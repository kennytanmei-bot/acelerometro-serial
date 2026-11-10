/*
 * Atividade Acelerometro - Serial
 * FRDM-KL25Z + MMA8451Q (I2C0), Zephyr 4.2
 *
 * 3 threads ligadas por filas de mensagens:
 *
 *   [thread_aquisicao] --q_acq--> [thread_integracao] --q_cin--> [thread_serial]
 *     le o MMA8451Q             calcula v e p                    envia CSV pela
 *     a cada 50 ms              (regra dos trapezios)            UART (OpenSDA)
 *
 * Linha enviada (compativel com leitor_serial.py, 9 colunas):
 *   ax,ay,az,vx,vy,vz,px,py,pz   (m/s^2, m/s, m)
 */
#include <zephyr/kernel.h>
#include <zephyr/device.h>
#include <zephyr/drivers/sensor.h>
#include <zephyr/drivers/i2c.h>
#include <zephyr/sys/printk.h>

/* ===== MMA8451Q: endereco e registradores ===== */
#define MMA8451Q_I2C_ADDR   0x1D
#define MMA8451Q_CTRL_REG1  0x2A
#define MMA8451Q_ACTIVE_BIT 0x01
/* DR[5:3] = 100b -> 50 Hz (combina com a amostragem de 50 ms) */
#define MMA8451Q_ODR        (0x4 << 3)

/* ===== Parametros da aquisicao/integracao ===== */
#define PERIODO_MS      50
#define DT              (PERIODO_MS / 1000.0f)
#define AMOSTRAS_CALIB  40       /* ~2 s com a placa parada */
#define LIMIAR_RUIDO    0.05f    /* m/s^2: abaixo disso considera 0 */

/* ===== Threads (menor numero = maior prioridade) ===== */
#define PRIO_AQUISICAO  4
#define PRIO_INTEGRACAO 5
#define PRIO_SERIAL     6

/* ===== Dispositivos ===== */
#if DT_NODE_EXISTS(DT_ALIAS(accel0))
static const struct device *const accel = DEVICE_DT_GET(DT_ALIAS(accel0));
#else
static const struct device *const accel = DEVICE_DT_GET(DT_NODELABEL(mma8451q));
#endif
static const struct device *const i2c_dev = DEVICE_DT_GET(DT_NODELABEL(i2c0));

/* ===== Dados trocados entre threads ===== */
struct vec3 { float x, y, z; };
struct amostra_cin { struct vec3 a, v, p; };

K_MSGQ_DEFINE(q_acq, sizeof(struct vec3), 8, 4);
K_MSGQ_DEFINE(q_cin, sizeof(struct amostra_cin), 8, 4);

/* ---------- Configuracao do ODR via I2C ---------- */
static void mma8451q_configurar_odr(void)
{
	uint8_t buf[2];
	int ret;

	/* 1. standby (obrigatorio para mudar o ODR) */
	buf[0] = MMA8451Q_CTRL_REG1;
	buf[1] = 0x00;
	ret = i2c_write(i2c_dev, buf, 2, MMA8451Q_I2C_ADDR);
	if (ret) {
		printk("ERRO standby MMA8451Q (%d)\n", ret);
		return;
	}

	/* 2. ODR = 50 Hz */
	buf[1] = MMA8451Q_ODR;
	ret = i2c_write(i2c_dev, buf, 2, MMA8451Q_I2C_ADDR);
	if (ret) {
		printk("ERRO ao configurar ODR (%d)\n", ret);
		return;
	}

	/* 3. ativa novamente */
	buf[1] = MMA8451Q_ODR | MMA8451Q_ACTIVE_BIT;
	ret = i2c_write(i2c_dev, buf, 2, MMA8451Q_I2C_ADDR);
	if (ret) {
		printk("ERRO ao ativar MMA8451Q (%d)\n", ret);
		return;
	}
}

/* ---------- Thread 1: aquisicao ---------- */
static void thread_aquisicao(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

	struct sensor_value x, y, z;
	struct vec3 s;
	int64_t proximo = k_uptime_get();

	while (1) {
		proximo += PERIODO_MS;

		if (sensor_sample_fetch(accel) == 0) {
			sensor_channel_get(accel, SENSOR_CHAN_ACCEL_X, &x);
			sensor_channel_get(accel, SENSOR_CHAN_ACCEL_Y, &y);
			sensor_channel_get(accel, SENSOR_CHAN_ACCEL_Z, &z);

			s.x = (float)sensor_value_to_double(&x);
			s.y = (float)sensor_value_to_double(&y);
			s.z = (float)sensor_value_to_double(&z);

			/* nao bloqueia: se a fila encher, descarta a amostra */
			k_msgq_put(&q_acq, &s, K_NO_WAIT);
		}

		/* periodo absoluto: nao acumula jitter */
		k_sleep(K_TIMEOUT_ABS_MS(proximo));
	}
}

/* ---------- Thread 2: integracao numerica ---------- */
static float zona_morta(float a)
{
	return (a > -LIMIAR_RUIDO && a < LIMIAR_RUIDO) ? 0.0f : a;
}

static void thread_integracao(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

	struct vec3 in;
	struct vec3 bias = {0, 0, 0};
	struct vec3 a_ant = {0, 0, 0};
	struct vec3 v = {0, 0, 0}, p = {0, 0, 0}, v_ant;
	struct amostra_cin out;

	/* Calibracao: placa parada; a media inclui offset e gravidade */
	for (int i = 0; i < AMOSTRAS_CALIB; i++) {
		k_msgq_get(&q_acq, &in, K_FOREVER);
		bias.x += in.x;
		bias.y += in.y;
		bias.z += in.z;
	}
	bias.x /= AMOSTRAS_CALIB;
	bias.y /= AMOSTRAS_CALIB;
	bias.z /= AMOSTRAS_CALIB;

	while (1) {
		k_msgq_get(&q_acq, &in, K_FOREVER);

		struct vec3 a = {
			zona_morta(in.x - bias.x),
			zona_morta(in.y - bias.y),
			zona_morta(in.z - bias.z),
		};

		v_ant = v;

		/* Regra dos trapezios: I[k] = I[k-1] + dt*(f[k-1] + f[k])/2 */
		v.x += DT * (a_ant.x + a.x) * 0.5f;
		v.y += DT * (a_ant.y + a.y) * 0.5f;
		v.z += DT * (a_ant.z + a.z) * 0.5f;

		p.x += DT * (v_ant.x + v.x) * 0.5f;
		p.y += DT * (v_ant.y + v.y) * 0.5f;
		p.z += DT * (v_ant.z + v.z) * 0.5f;

		a_ant = a;

		out.a = a;
		out.v = v;
		out.p = p;
		k_msgq_put(&q_cin, &out, K_NO_WAIT);
	}
}

/* ---------- Thread 3: envio pela serial ---------- */
static void thread_serial(void *p1, void *p2, void *p3)
{
	ARG_UNUSED(p1); ARG_UNUSED(p2); ARG_UNUSED(p3);

	struct amostra_cin s;

	/* tempo para abrir o leitor_serial.py antes do cabecalho */
	k_sleep(K_SECONDS(3));
	printk("ax,ay,az,vx,vy,vz,px,py,pz\n");

	while (1) {
		k_msgq_get(&q_cin, &s, K_FOREVER);

		printk("%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f\n",
		       (double)s.a.x, (double)s.a.y, (double)s.a.z,
		       (double)s.v.x, (double)s.v.y, (double)s.v.z,
		       (double)s.p.x, (double)s.p.y, (double)s.p.z);
	}
}

/* As threads iniciam 1 s apos o boot, depois da configuracao em main() */
K_THREAD_DEFINE(tid_aq,  1024, thread_aquisicao,  NULL, NULL, NULL,
		PRIO_AQUISICAO, 0, 1000);
K_THREAD_DEFINE(tid_int, 1024, thread_integracao, NULL, NULL, NULL,
		PRIO_INTEGRACAO, 0, 1000);
K_THREAD_DEFINE(tid_ser, 1536, thread_serial,     NULL, NULL, NULL,
		PRIO_SERIAL, 0, 1000);

int main(void)
{
	if (!device_is_ready(accel)) {
		printk("Sensor MMA8451Q nao esta pronto!\n");
		return 0;
	}
	if (!device_is_ready(i2c_dev)) {
		printk("I2C nao esta pronto!\n");
		return 0;
	}

	mma8451q_configurar_odr();
	k_msleep(200);   /* estabiliza apos reativar */

	return 0;
}