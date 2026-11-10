import pandas as pd
import matplotlib.pyplot as plt
import numpy as np

# 1. Carregar o arquivo CSV
try:
    # Lê a primeira linha para verificar se já contém o cabeçalho correto
    primeira_linha = pd.read_csv('dados_cinematicos.csv', nrows=1)
    
    colunas_esperadas = ['ax', 'ay', 'az', 'vx', 'vy', 'vz', 'px', 'py', 'pz']
    
    # Se a primeira coluna não for 'ax', define os nomes manualmente
    if 'ax' not in [col.strip() for col in primeira_linha.columns]:
        df = pd.read_csv('dados_cinematicos.csv', names=colunas_esperadas, header=None)
    else:
        df = pd.read_csv('dados_cinematicos.csv')
        df.columns = df.columns.str.strip()  # Remove espaços acidentais nos nomes

    # Garante que as colunas sejam tratadas como números (descarta linhas corrompidas se houver)
    for col in colunas_esperadas:
        df[col] = pd.to_numeric(df[col], errors='coerce')
    df = df.dropna()

except FileNotFoundError:
    print("Erro: O arquivo 'dados_cinematicos.csv' não foi encontrado.")
    exit()

# 2. Criar vetor de tempo baseado em 50ms (0.05s) por amostra
tempo = np.arange(len(df)) * 0.05

# 3. Configurar a figura com 3 subgráficos
fig, axs = plt.subplots(3, 1, figsize=(10, 12), sharex=True)

# --- Gráfico de Aceleração ---
axs[0].plot(tempo, df['ax'], label='ax', color='r')
axs[0].plot(tempo, df['ay'], label='ay', color='g')
axs[0].plot(tempo, df['az'], label='az', color='b')
axs[0].set_title('Aceleração ao longo do Tempo (m/s²)')
axs[0].set_ylabel('Aceleração (m/s²)')
axs[0].legend(loc='upper right')
axs[0].grid(True, linestyle='--', alpha=0.7)

# --- Gráfico de Velocidade ---
axs[1].plot(tempo, df['vx'], label='vx', color='r')
axs[1].plot(tempo, df['vy'], label='vy', color='g')
axs[1].plot(tempo, df['vz'], label='vz', color='b')
axs[1].set_title('Velocidade Estimada (m/s)')
axs[1].set_ylabel('Velocidade (m/s)')
axs[1].legend(loc='upper right')
axs[1].grid(True, linestyle='--', alpha=0.7)

# --- Gráfico de Posição ---
axs[2].plot(tempo, df['px'], label='px', color='r')
axs[2].plot(tempo, df['py'], label='py', color='g')
axs[2].plot(tempo, df['pz'], label='pz', color='b')
axs[2].set_title('Posição Estimada (m)')
axs[2].set_ylabel('Posição (m)')
axs[2].set_xlabel('Tempo (segundos)')
axs[2].legend(loc='upper right')
axs[2].grid(True, linestyle='--', alpha=0.7)

# 4. Salvar e exibir imagem
plt.tight_layout()
plt.savefig('graficos_atividade2.png', dpi=300)
print("Gráfico salvo com sucesso como 'graficos_atividade2.png'!")

plt.show()