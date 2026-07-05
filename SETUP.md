# EA CHOCA+ v7.0 Aurora Cloud

Estrutura do projeto para PlatformIO com servidor e clientes descentralizados.

## 📋 Estrutura de Pastas

```
chocadeiras-inteligentes/
├── platformio.ini              # Configuração com 2 environments
├── README.md
├── README_v7.md               # Documentação completa
├── server/
│  └── server_esp32.cpp         # Código do servidor (Hub)
├── client/
│  └── client_esp32.cpp         # Código do cliente (Chocadeira)
└── include/
   └── (para headers compartilhados - opcional)
```

## 💻 Como Compilar

### Compilar SERVIDOR
```bash
cd ~/seu-projeto
platformio run -e esp32-server --target upload
```

### Compilar CLIENTE
```bash
cd ~/seu-projeto
platformio run -e esp32-client --target upload
```

### Monitorar logs
```bash
platformio device monitor -b 115200
```

## ⚠️ IMPORTANTE

- Cada environment compila apenas o seu arquivo correspondente
- O `src_filter` exclui os arquivos não usados
- Não tente compilar cliente com `esp32-server` ou vice-versa
