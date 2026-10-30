# NFS Carbon (iPhone)

O app do **SpeedBreaker: Carbon** (projeto de fã que traz o *Need for Speed: Carbon* de Xbox 360 como app nativo), no
iPhone pelo **TestFlight** de uma conta Apple paga, com **controles por toque** nossos (pasta `Toque/`).

Este repositório não tem nada do jogo nem da EA. O workflow baixa o IPA público do projeto, confere, ajusta, põe a camada
de toque, assina com a assinatura gerenciada pela Apple e envia ao TestFlight interno. O jogo se instala dentro do app, a
partir da imagem do disco (Xbox 360, *Collector's Edition* americana).

**Estado:** Entrega 2, os controles por toque, agora no desenho do NFSU2 e com os botões móveis. A Entrega 1 (o caminho
até o iPhone) já funcionou.

---

## Entrega 2, controles no desenho do NFSU2: subir e testar

### 1. Subir (PowerShell)

```powershell
cd C:\claudin\gtm\2026\random\speedbreaker-ios
git add -A
git status
```

No `git status`, o esperado: modificados `LEIA-ME.md`, `Toque/Toque.m`, `Toque/nucleo.c`, `Toque/nucleo.h` e
`testes/testar_nucleo.c` (a correção dos rótulos e o EQUIPE, que estavam na pasta sem subir, vão juntos, já refeitos).
O `PLANO-speedbreaker-ios.md` continua de fora. Se estiver certo:

```powershell
git commit -m "Controles no desenho do NFSU2 e botoes moveis"
git push
```

### 2. Rodar

1. **Actions** › **TestFlight NFS Carbon** › **Run workflow**. Em "Controles por toque", deixe **sim** › **Run workflow**.
2. Uns 6 minutos. O resumo diz "build N enviada, com os controles por toque".
3. Mais 10 a 20 minutos de processamento na Apple, e a build aparece no TestFlight.

**Se falhar:** o passo **Resumo dos erros** mostra os erros de compilação da camada. Me mande o texto.

### 3. Testar

1. TestFlight › NFS Carbon › **Atualizar**. O jogo instalado, os saves e os ajustes ficam. A antiga "Transparência"
   não vem junto: a nova **Opacidade** começa em 100%, porque o desenho do NFSU2 já é meio transparente.
2. Abra o jogo. Os controles agora são os do NFSU2: retângulos e redondos pretos a 40%, texto branco em itálico
   negrito, verde-limão enquanto apertado. O **≡** (os ajustes da camada) fica no canto de cima, à esquerda, no lugar da
   engrenagem; nas telas cheias do SpeedBreaker fica só ele.
3. **Botão de direção** (canto de cima, à direita): cada toque passa para o próximo jeito de dirigir, botões ◀ ▶,
   analógico e inclinação. Um aviso no alto diz qual ficou, e já fica salvo.
4. **Mover os botões:** ≡ › **Mover os botões** (veja abaixo). Mude um botão de lugar e de tamanho, toque em **Pronto**,
   feche e abra o app: ele continua onde você deixou.
5. Jogue uma corrida.

**Me mande:** um print do Menu, um da pilotagem e um do Mover os botões, e se o toque ficou mais fácil de acertar.

**Se o app fechar sozinho ao abrir:** rode o workflow de novo com "Controles por toque: **não**" (volta a ser o app da
Entrega 1) e me mande o relatório do travamento: em Ajustes › Privacidade e Segurança › Análise e Melhorias › **Dados de
Análise**, o arquivo mais novo que começa com "SpeedBreaker" (abra e use o botão de compartilhar). Se o TestFlight
perguntar se quer enviar o travamento, aceite também.

### Como usar os controles

As medidas, as cores e os ícones são os do port do NFSU2. O que o Carbon tem a mais entra no mesmo desenho.

- **Menu:** direcional em cruz embaixo à esquerda; Voltar (B) sobre OK (A) embaixo à direita, com X e Y em cima deles;
  no alto, LB e RB nos cantos, e Back, **Pilotar** e Start no meio. **Pilotar** troca para a pilotagem.
- **Pilotar:**
  - fileira de cima: ≡, Pausa, Câmera, Reset, Mapa, SMS e **Equipe** (ativa o parceiro); à direita, **Menu** (volta
    para o modo Menu) e o botão de direção. A Pausa já muda para Menu;
  - direção: ◀ ▶ embaixo à esquerda (o dedo desliza entre as duas), ou o analógico (o polegar pousa em qualquer lugar do
    canto de baixo, à esquerda, e a base vem para debaixo dele), ou inclinar o iPhone como um volante (o jeito que ele
    estiver quando você começa a pilotar conta como reto; o botão com o alvo, ao lado, acerta de novo);
  - **Gás** (o alto) e **Freio** (também é a ré) embaixo à direita, **N₂O** em cima do Gás (já acelera junto),
    **Freio de mão** em cima do Freio e **Speedbreaker** à esquerda dele. O dedo desliza entre Gás, Freio e N₂O sem
    levantar; fora deles, fica no último;
  - na inclinação, o Freio e o Freio de mão vão para baixo do polegar esquerdo, e o N₂O para o lado do Gás;
  - Câmera: um toque troca a câmera; segurada, olha para trás;
  - **Entrar**, em cima das setas: entra na corrida ou na loja;
  - com o câmbio manual ligado nos ajustes: as marchas, em cima das setas.
- **Mover os botões** (≡ › Mover os botões):
  - todos os botões ganham um contorno tracejado, e o painel do meio mostra o escolhido (contorno verde) e o tamanho;
  - um dedo arrasta; para mudar o tamanho, com um dedo no botão, ponha o segundo em qualquer lugar da tela e abra ou
    feche (de 50 a 250%);
  - Menu, Botões, Analógico e Inclinação, no painel: qual tela arrumar. Cada uma guarda as suas posições;
  - **Padrão** volta a tela à mostra ao lugar e ao tamanho padrão (pergunta "Tem certeza?"); **Cancelar** desfaz o que
    mudou desde que entrou (pergunta se mudou algo); **Pronto** salva;
  - enquanto você move, nada vai para o jogo, e o painel some enquanto um dedo está num botão.
- **Controle de verdade** (Bluetooth): os toques somem e fica só o ≡; o jogo passa a usar o controle. Desconectou, os
  toques voltam.
- **≡:** Mover os botões, direção, tamanho (80 a 150%, até o que cabe na tela: no iPhone 17, 114%), opacidade,
  vibração (toques e jogo, só toques, desligada), câmbio manual, "Pausa muda para o modo Menu", reatribuir botões, abrir
  os ajustes do SpeedBreaker, Diagnóstico e restaurar o padrão (tudo, inclusive as posições; pergunta "Tem certeza?").
- Mapa, SMS e as marchas ainda vão ser conferidos no jogo. Se algum não funcionar: ≡ › Reatribuir botões.

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
  e os ajustes e as posições dos controles ficam.
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
| `Toque/nucleo.c`, `nucleo.h` | o miolo dos controles em C: as medidas do NFSU2, toques, modos, mover os botões, o que cada toque aperta |
| `Toque/ponte_sdl.c`, `ponte_sdl.h` | a ligação com o SDL e com o SpeedBreaker que estão dentro do app |
| `Toque/Toque.m` | a parte de iOS: janela, o desenho e os ícones do NFSU2, toques, vibração, inclinação, ajustes, posições salvas e diagnóstico |
| `Toque/Info.plist`, `PrivacyInfo.xcprivacy` | do framework (o workflow completa as versões do Xcode) |
| `Toque/SDL/` | 31 cabeçalhos do SDL 3.4.16, a mesma versão do app (licença zlib) |
| `testes/testar_montar.py` | `python3 testes/testar_montar.py <ipa>` |
| `testes/testar_injetar.py` | `python3 testes/testar_injetar.py <executável do app>` |
| `testes/testar_nucleo.c`, `testar_ponte.c` | o comando de compilar e rodar está no topo de cada um (qualquer compilador C) |

## O que foi conferido antes de subir

- Núcleo dos controles: 22.475 checagens. As medidas do NFSU2 no iPhone 17, uma a uma. Em 6 telas (do iPhone SE ao
  iPad), 11 combinações de ajustes e os dois modos: nada sai das margens, nenhum controle fica a menos de 4 pontos de
  outro (no tamanho 100%), e cada um é acertado no próprio centro. E os toques: aperto mínimo de 150 ms, pedais e setas deslizando como no
  NFSU2, rampa da direção, analógico, inclinação, câmera, pausa, modos, botão de direção, mover, pinça, limites da tela,
  posições por tela, nomes guardados e ajustes estragados. 35 erros plantados de propósito: os 35 pegos.
- Ícones: os 16 redesenhados para o iOS, comparados pixel a pixel com os do NFSU2 (diferença abaixo de 0,1%, só o
  serrilhado).
- Uma prévia de cada tela desenhada a partir das posições reais do núcleo.
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
