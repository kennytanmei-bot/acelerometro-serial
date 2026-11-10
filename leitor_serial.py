import serial
import csv
import time
import sys

# ================= CONFIGURAÇÕES =================
PORTA_SERIAL = 'COM19'
BAUD_RATE = 115200
ARQUIVO_CSV = 'dados_cinematicos.csv'
# =================================================

def iniciar_leitura():
    try:
        conexao = serial.Serial(PORTA_SERIAL, BAUD_RATE, timeout=1)
        print(f"Conectado na porta {PORTA_SERIAL} a {BAUD_RATE} bps.")
        print(f"Gravando dados em '{ARQUIVO_CSV}'. Pressione Ctrl+C para parar.")

        with open(ARQUIVO_CSV, mode='w', newline='') as arquivo:
            escritor_csv = csv.writer(arquivo)
            
            cabecalho_lido = False

            while True:
                if conexao.in_waiting > 0:
                    linha = conexao.readline().decode('utf-8').strip()
                    
                    if not linha:
                        continue
                        
                    dados = linha.split(',')
                    
                    if not cabecalho_lido and len(dados) == 9:
                        escritor_csv.writerow(dados)
                        print("Cabeçalho identificado e gravado:", dados)
                        cabecalho_lido = True
                    elif cabecalho_lido and len(dados) == 9:
                        escritor_csv.writerow(dados)
                        print(f"Dado salvo: {dados}")
                        
    except serial.SerialException as e:
        print(f"Erro ao abrir a porta serial: {e}")
        print("Verifique se a placa está conectada e se a porta COM está correta.")
    except KeyboardInterrupt:
        print("\nLeitura encerrada pelo usuário.")
    finally:
        if 'conexao' in locals() and conexao.is_open:
            conexao.close()
            print("Porta serial fechada.")

if __name__ == "__main__":
    iniciar_leitura()