# NFS Carbon (iPhone)

O app do **SpeedBreaker: Carbon** (projeto de fã que traz o *Need for Speed: Carbon* de Xbox 360 como app nativo), no
iPhone pelo **TestFlight** de uma conta Apple paga, com **controles por toque** nossos (pasta `Toque/`).

Este repositório não tem nada do jogo nem da EA. O workflow baixa o IPA público do projeto, confere, ajusta, põe a camada
de toque, assina com a assinatura gerenciada pela Apple e envia ao TestFlight interno. O jogo se instala dentro do app, a
partir da imagem do disco (Xbox 360, *Collector's Edition* americana).

**Estado:** Entrega 2, os controles por toque. A Entrega 1 (o caminho até o iPhone) já funcionou.

---

## Entrega 2: subir e testar

### 1. Subir (PowerShell)

```powershell
cd C:\claudin\gtm\2026\random\speedbreaker-ios
Move-Item -Force PARA-A-PASTA-.github-workflows\testflight-carbon.yml .github\workflows\testflight-carbon.yml
Remove-Item PARA-A-PASTA-.github-workflows
git add -A
git status
```

No `git status`, o esperado:
- modificados: `.gitattributes`, `.gitignore`, `.github/workflows/testflight-carbon.yml`, `LEIA-ME.md`,
  `scripts/montar.py` e `scripts/reassinar.sh`;
- novos: a pasta `Toque/`, `scripts/injetar.py`, `testes/testar_injetar.py`, `testes/testar_nucleo.c` e
  `testes/testar_ponte.c`.

O `PLANO-speedbreaker-ios.md` continua de fora. Se estiver certo:

```powershell
git commit -m "Entrega 2: controles por toque"
git push
```

### 2. Rodar

1. **Actions** › **TestFlight NFS Carbon** › **Run workflow**. Em "Controles por toque", deixe **sim** › **Run workflow**.
2. Leva uns 6 minutos (agora compila a camada antes de enviar). O resumo diz "build N enviada, com os controles por
   toque".
3. Mais 10 a 20 minutos de processamento na Apple, e a build aparece no TestFlight.

**Se falhar:** o passo **Resumo dos erros** mostra os erros de compilação da camada (em "controles por toque") e os do
envio. Me mande o texto, e o e-mail do App Store Connect se chegar algum (códigos `ITMS-`).

### 3. Testar sem o disco

1. TestFlight › NFS Carbon › **Atualizar**. O jogo instalado e os saves ficam (por enquanto não tem nenhum).
2. Abra. O instalador aparece como antes, agora com uma **engrenagem** pequena no canto de cima, à direita. Nas telas
   cheias do SpeedBreaker (instalador, avisos) fica só ela: ali o SpeedBreaker usa o toque direto.
3. Engrenagem › **Diagnóstico**. O esperado:
   - Funções do app: 13 de 13
   - SDL dos controles: ligado
   - Controle virtual: conectado
   - Aberto pelo jogo: sim
   - Outros controles: 0 (se houver um controle Bluetooth ligado, desligue para este teste)
   - Memória que o app ainda pode usar: alguns GB (mostra se a permissão de memória a mais veio)

   Toque em **Copiar** e me mande o texto.
4. **Testar a vibração (1 s):** o iPhone vibra por um segundo.
5. **Controles sempre à mostra:** toque até ficar "sim" e volte. Os controles do modo Menu aparecem por cima do instalador.
   - No direcional, aperte para baixo e para cima: o destaque anda entre os botões do instalador (*Browse for image...*,
     *Browse for folder...*, *Rescan*). Com o destaque no *Rescan*, aperte **OK**: ele procura de novo. Isso mostra o
     caminho inteiro funcionando: toque › controle virtual › jogo.
   - Toque em **PILOTAR**: aparecem os controles de pilotagem. Sem o jogo não dá para dirigir; é para ver tamanho e
     posição. Tire um print de cada modo.
   - Na engrenagem, experimente **Direção** (Analógico, Inclinar o iPhone), **Tamanho** e **Transparência**.
   - O "sempre à mostra" não fica salvo: fechou o app, volta ao normal.
6. O toque com **3 dedos** (fora dos botões da camada) continua abrindo os ajustes do SpeedBreaker.

**Me mande:** o texto do Diagnóstico, os prints dos dois modos e se o direcional e o OK mexeram no instalador.

**Se o app fechar sozinho ao abrir:** rode o workflow de novo com "Controles por toque: **não**" (volta a ser o app da
Entrega 1) e me mande o relatório do travamento: em Ajustes › Privacidade e Segurança › Análise e Melhorias › **Dados de
Análise**, o arquivo mais novo que começa com "SpeedBreaker" (abra e use o botão de compartilhar). Se o TestFlight
perguntar se quer enviar o travamento, aceite também.

### Como usar os controles

- **PILOTAR / MENU**, no alto, no meio: troca o modo. A Pausa (❚❚) pilotando já muda para Menu.
- **Menu:** direcional, OK (A), VOLTAR (B), X, Y, LB e RB, BACK e START.
- **Pilotar:**
  - direção: ◀ ▶, ou analógico (onde o polegar pousar, na metade esquerda), ou inclinar o iPhone como um volante (o
    jeito que ele estiver quando você começa a pilotar conta como reto; o botão **RETO** acerta de novo);
  - GÁS, FREIO (também é a ré) e NITRO (já acelera junto): o dedo desliza entre os três sem levantar; o mesmo entre ◀ e ▶;
  - FREIO DE MÃO, SPEEDBREAKER e PARCEIRO;
  - CÂM: um toque troca a câmera; segurado, olha para trás;
  - ENTRAR: entra na corrida ou na loja;
  - **+**: abre RESET, MAPA e SMS;
  - com o câmbio manual ligado nos ajustes: M+ e M−.
- **Controle de verdade** (Bluetooth): os toques somem e fica só a engrenagem; o jogo passa a usar o controle.
  Desconectou, os toques voltam.
- **Engrenagem:** direção, tamanho (80 a 150%), transparência, vibração (toques e jogo, só toques, desligada), câmbio
  manual, "Pausa muda para o modo Menu", reatribuir botões, abrir os ajustes do SpeedBreaker, Diagnóstico e restaurar o
  padrão (pergunta "Tem certeza?").
- Mapa, SMS e as marchas ainda vão ser conferidos no jogo, na Entrega 3. Se algum não funcionar: engrenagem › Reatribuir
  botões.

---

## Primeira vez (Entrega 1, já feito)

Fica aqui para consulta.

### 1. App ID (developer.apple.com)

1. developer.apple.com › Account › **Certificates, IDs & Profiles** › **Identifiers** › **+**.
2. **App IDs** › Continue › **App** › Continue.
3. Description: `NFS Carbon`. Bundle ID: **Explicit**, `com.gtm.nfsc`.
4. Em Capabilities, marque as duas de memória:
   - **Increased Memory Limit**;
   - **Extended Virtual Address Space** (em algumas versões da página aparece como *Extended Virtual Addressing*).
5. Continue › **Register**.

### 2. App no App Store Connect

1. appstoreconnect.apple.com › **Apps** › **+** › **New App**.
2. Preencha:
   - Platforms: **iOS**;
   - Name: um nome livre, por exemplo `NFS Carbon GTM`. Ele não aparece no iPhone; se disser que já existe, troque;
   - Primary Language: **Portuguese (Brazil)**;
   - Bundle ID: o `com.gtm.nfsc` do passo 1;
   - SKU: `nfsc`;
   - User Access: **Full Access**.
3. **Create**.
4. Aba **TestFlight** › **Test Information**: preencha a descrição (ex.: "Uso pessoal") e o e-mail de feedback › Save.
5. **Internal Testing** › **+**: um grupo (ex.: `Eu`) com você dentro e a distribuição automática ligada.

### 3. Repositório no GitHub

1. github.com › **New repository**: nome `speedbreaker-ios`, **Public**, sem README, sem .gitignore e sem licença.
2. No repositório: **Settings** › **Secrets and variables** › **Actions** › **New repository secret**. Os 4, com os
   mesmos valores do `nfios`:

   | Secret | Valor |
   |---|---|
   | `ASC_KEY_ID` | o Key ID da chave da API do App Store Connect |
   | `ASC_ISSUER_ID` | o Issuer ID |
   | `ASC_KEY_P8` | o texto inteiro do `AuthKey_XXXX.p8`, com as linhas BEGIN e END |
   | `APPLE_TEAM_ID` | o Team ID (10 caracteres, em Membership details) |

### 4. Subir a pasta (PowerShell)

```powershell
cd C:\claudin\gtm\2026\random\speedbreaker-ios
New-Item -ItemType Directory -Force .github\workflows | Out-Null
Move-Item -Force PARA-A-PASTA-.github-workflows\testflight-carbon.yml .github\workflows\testflight-carbon.yml
Remove-Item PARA-A-PASTA-.github-workflows
git init -b main
git add .
git commit -m "Entrega 1: TestFlight do NFS Carbon"
git remote add origin https://github.com/GTMoraes/speedbreaker-ios.git
git push -u origin main
```

### 5. Rodar e instalar

Actions › TestFlight NFS Carbon › Run workflow. Depois, no iPhone: TestFlight › NFS Carbon › Instalar.

---

## Dia a dia

- **A build vence em 90 dias** no TestFlight: Actions › Run workflow de novo. Instala por cima; o jogo instalado, os saves
  e os ajustes dos controles ficam.
- **Versão nova do SpeedBreaker:** não troque o `config/carbon.conf` sozinho. Me avise; eu confiro a versão nova (hash,
  permissões de memória e as funções que os controles por toque usam) e atualizo. O próprio workflow também confere as
  funções e para se faltar alguma.
- **Push não gera build.** Só o Run workflow.

## Para jogar

Precisa da imagem do seu disco de Xbox 360, a **Collector's Edition americana** (a capa diz *Collector's Edition*). A
edição comum, as outras regiões e a versão de PC não instalam. A imagem pode ser `.iso` completa, XISO ou a pasta com os
arquivos extraídos. Para levar ao iPhone:

- pendrive ou SSD USB-C formatado em **exFAT** (em FAT32 o arquivo é cortado em 4 GB e o instalador recusa) e, no
  instalador, **Browse**: ele lê direto do pendrive, sem copiar;
- ou copiar para Arquivos › No Meu iPhone › NFS Carbon, onde o instalador acha sozinho.

O instalador confere cada arquivo contra o disco conhecido. Depois, se copiou a imagem para o iPhone, pode apagá-la.

---

## Como funciona

1. `scripts/reassinar.sh` baixa o IPA da versão fixada no `config/carbon.conf` e confere o `.sha256` publicado com ele.
2. `scripts/montar.py` confere o sha256 do IPA e do executável, confere que o app pede exatamente as duas permissões de
   memória do `config/carbon.entitlements`, extrai o app para um `.xcarchive` e ajusta o Info.plist:
   - bundle ID `com.gtm.nfsc` e nome na tela "NFS Carbon";
   - número da build = número da execução do Actions;
   - só iPhone;
   - `ITSAppUsesNonExemptEncryption = false` (sem a pergunta de criptografia a cada build);
   - frases de câmera, microfone e Bluetooth (o SDL3, que vem dentro do app, cita essas APIs; o jogo não usa nenhuma) e
     de movimento (a direção por inclinação; ler o sensor não pede permissão, a frase só evita cobrança por engano).
3. Com "Controles por toque: sim":
   - compila a camada `Toque.framework` (Objective-C e C) com o Xcode do runner e põe dentro do app, com um Info.plist e
     um manifesto de privacidade próprios;
   - confere que a camada não importa nada do SDL na ligação: ela acha as funções do app pelo nome quando o app abre;
   - `scripts/injetar.py` confere que o executável exporta as 13 funções que a camada usa, tira a assinatura provisória e
     acrescenta um comando de carga *fraco* do framework. Se o framework faltar, o app abre do mesmo jeito, sem os
     toques.
4. Assinatura provisória com as permissões de memória; depois o `xcodebuild -exportArchive` assina com o certificado
   de distribuição gerenciado pela Apple e envia ao TestFlight interno.

O jogo não muda: o executável é o oficial, com um comando de carga a mais; o resto do app é igual ao oficial.

**Quando o app abre:** a camada espera o SDL do app ligar os controles e cria um **controle virtual** de Xbox 360 dentro
dele (o mesmo tipo que o runtime do SpeedBreaker usa nos próprios testes). Os botões são desenhados numa janela
transparente por cima do jogo; cada toque vira botão ou eixo desse controle, e o toque fora de um botão passa direto para
o jogo. A vibração que o jogo manda para o controle vira vibração do iPhone.

| Arquivo | O que é |
|---|---|
| `.github/workflows/testflight-carbon.yml` | o workflow (só roda à mão; escolha com ou sem os controles por toque) |
| `config/carbon.conf` | versão do SpeedBreaker, hashes, bundle ID e nome na tela |
| `config/carbon.entitlements` | as duas permissões de memória |
| `scripts/reassinar.sh` | os passos no runner macOS |
| `scripts/montar.py` | a parte de arquivos do app (Python, sem pacotes extras) |
| `scripts/injetar.py` | o comando de carga da camada no executável (Python, sem pacotes extras) |
| `Toque/nucleo.c`, `nucleo.h` | o miolo dos controles em C: posições, toques, modos, o que cada toque aperta |
| `Toque/ponte_sdl.c`, `ponte_sdl.h` | a ligação com o SDL e com o SpeedBreaker que estão dentro do app |
| `Toque/Toque.m` | a parte de iOS: janela, desenho, toques, vibração, inclinação, ajustes e diagnóstico |
| `Toque/Info.plist`, `PrivacyInfo.xcprivacy` | do framework (o workflow completa as versões do Xcode) |
| `Toque/SDL/` | 31 cabeçalhos do SDL 3.4.16, a mesma versão do app (licença zlib) |
| `testes/testar_montar.py` | `python3 testes/testar_montar.py <ipa>` |
| `testes/testar_injetar.py` | `python3 testes/testar_injetar.py <executável do app>` |
| `testes/testar_nucleo.c`, `testar_ponte.c` | o comando de compilar e rodar está no topo de cada um (qualquer compilador C) |

## O que foi conferido antes de subir

- Núcleo dos controles: 3.268 checagens (posições sem sobrepor nem sair da tela em todos os tamanhos, toques, pedais
  deslizando, direção com rampa, analógico, inclinação, câmera, pausa, modos, reatribuição, ajustes estragados).
- Ponte com o SDL: 97 checagens com um SDL de mentira (o controle criado como o do runtime, gatilhos soltos ao conectar,
  só o que mudou é enviado, cada botão no botão certo, rumble, falhas), mais 11 com duas funções faltando (a camada fica
  desligada sem quebrar nada). Os dois testes rodaram também com os detectores de erro de memória do compilador.
- `injetar.py` contra o executável oficial: 33 checagens, e o resultado relido por outro leitor de Mach-O (llvm-otool):
  só sai a assinatura provisória e entra o comando de carga, o resto é igual byte a byte.
- `montar.py`: 126 checagens contra o IPA oficial.
- Em todos: cada tipo de erro testado quebrando o código de propósito.
- `Toque.m`: sintaxe e tipos conferidos com cabeçalhos dublê das APIs da Apple que ele usa. A compilação de verdade só
  existe no Mac da GitHub.
- `reassinar.sh` simulado de ponta a ponta com dublês do Xcode: com e sem a camada, valor inválido, erro de compilação
  (aparece no resumo) e framework importando o SDL (recusado). shellcheck sem avisos; YAML válido.
- Não dá para conferir fora do Mac da GitHub e do iPhone: a compilação com o SDK do iOS, a assinatura, se a Apple aceita
  o app com o framework e o app rodando.

---

SpeedBreaker: Carbon é um projeto de fã (project(u), GPL-3.0), sem relação com a Electronic Arts.
*Need for Speed* e *Carbon* são marcas da EA. Este repositório não contém nem distribui arquivos do jogo.
Os cabeçalhos em `Toque/SDL/` são do SDL (licença zlib, em `Toque/SDL/LICENSE.txt`).
