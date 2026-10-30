# NFS Carbon (iPhone)

O app do **SpeedBreaker: Carbon** (projeto de fã que traz o *Need for Speed: Carbon* de Xbox 360 como app nativo), no
iPhone pelo **TestFlight** de uma conta Apple paga.

Este repositório não tem nada do jogo nem da EA. O workflow baixa o IPA público do projeto, confere, ajusta, assina com
a assinatura gerenciada pela Apple e envia ao TestFlight interno. O jogo se instala dentro do app, a partir da imagem do
disco (Xbox 360, *Collector's Edition* americana).

**Estado:** Entrega 1, o caminho até o iPhone. Os controles por toque entram na Entrega 2.

---

## Primeira vez

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
2. No repositório: **Settings** › **Secrets and variables** › **Actions** › **New repository secret**. Crie os 4, com os
   mesmos valores que você pôs no `nfios`:

   | Secret | Valor |
   |---|---|
   | `ASC_KEY_ID` | o Key ID da chave da API do App Store Connect |
   | `ASC_ISSUER_ID` | o Issuer ID |
   | `ASC_KEY_P8` | o texto inteiro do `AuthKey_XXXX.p8`, com as linhas BEGIN e END (abra no Bloco de Notas e copie tudo) |
   | `APPLE_TEAM_ID` | o Team ID (10 caracteres, em Membership details) |

   Se você não tiver mais o arquivo `.p8`: App Store Connect › **Users and Access** › **Integrations** › **App Store
   Connect API** › **Team Keys** › **+**, acesso **Admin**. Baixe o `.p8` (só deixa baixar uma vez) e use o Key ID e o
   Issuer ID dela. A chave do `nfios` continua valendo lá.

### 4. Subir a pasta (PowerShell)

```powershell
cd C:\claudin\gtm\2026\random\speedbreaker-ios
New-Item -ItemType Directory -Force .github\workflows | Out-Null
Move-Item -Force PARA-A-PASTA-.github-workflows\testflight-carbon.yml .github\workflows\testflight-carbon.yml
Remove-Item PARA-A-PASTA-.github-workflows
git init -b main
git add .
git status
```

No `git status`, confira a lista: `.gitattributes`, `.github/`, `.gitignore`, `LEIA-ME.md`, `config/`, `scripts/` e
`testes/`. O
`PLANO-speedbreaker-ios.md` fica de fora de propósito (está no `.gitignore`). Se estiver certo:

```powershell
git commit -m "Entrega 1: TestFlight do NFS Carbon"
git remote add origin https://github.com/GTMoraes/speedbreaker-ios.git
git push -u origin main
```

Se o `git remote add` disser "remote origin already exists": `git remote set-url origin
https://github.com/GTMoraes/speedbreaker-ios.git` e o `git push -u origin main` de novo.

O push não gera build: o workflow só roda quando você manda.

### 5. Rodar

1. No repositório: **Actions** › **TestFlight NFS Carbon** › **Run workflow** › **Run workflow**.
2. Leva uns 5 minutos. No fim, o resumo da execução diz "build N enviada".
3. A Apple processa por mais 10 a 20 minutos, e a build aparece no app TestFlight do iPhone.

**Se falhar:** abra a execução, o passo **Resumo dos erros**, e me mande o texto. Se chegar e-mail do App Store Connect
sobre a build (com códigos `ITMS-`), me mande também.

### 6. Primeiro teste no iPhone

1. App **TestFlight** › **NFS Carbon** › **Instalar**.
2. Abra. O esperado é o instalador do SpeedBreaker, em inglês, procurando a imagem do disco. Sem a imagem, ele fica
   nessa tela, e está certo.
3. Se aparecer **This copy of SpeedBreaker can't start**: tire um print (a linha *Details* diz com que permissões de
   memória o app veio) e me mande.
4. Opcional, para confirmar as permissões de memória: toque com **3 dedos** na tela; se abrir os ajustes, vá em
   **Advanced** › **Save Bug Report**. O zip aparece em Arquivos › No Meu iPhone › NFS Carbon. Me mande o zip.

---

## Dia a dia

- **A build vence em 90 dias** no TestFlight: Actions › Run workflow de novo. Instala por cima; o jogo instalado e os
  saves ficam.
- **Versão nova do SpeedBreaker:** não troque o `config/carbon.conf` sozinho. Me avise; eu confiro a versão nova (hash,
  permissões de memória e, a partir da Entrega 2, as funções que os controles por toque usam) e atualizo.
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
   - frases de câmera, microfone e Bluetooth: o SDL3, que vem dentro do app, cita essas APIs. O jogo não usa nenhuma.
3. Assinatura provisória com as permissões de memória; depois o `xcodebuild -exportArchive` assina com o certificado
   de distribuição gerenciado pela Apple e envia ao TestFlight interno.

O executável e o resto do app ficam iguais aos oficiais, byte a byte.

| Arquivo | O que é |
|---|---|
| `.github/workflows/testflight-carbon.yml` | o workflow (só roda à mão) |
| `config/carbon.conf` | versão do SpeedBreaker, hashes, bundle ID e nome na tela |
| `config/carbon.entitlements` | as duas permissões de memória |
| `scripts/reassinar.sh` | os passos no runner macOS |
| `scripts/montar.py` | a parte de arquivos (Python, sem pacotes extras) |
| `testes/testar_montar.py` | testa o `montar.py` contra o IPA oficial: `python3 testes/testar_montar.py <ipa>` |

## O que foi conferido antes de subir

- `montar.py` contra o IPA oficial v0.1.0: 125 checagens. O app montado é o oficial arquivo por arquivo (com as mesmas
  permissões), só o Info.plist muda, e só nas chaves previstas. IPA, executável, permissões, build e Team ID errados são
  recusados. Cada checagem foi testada quebrando o script de propósito.
- `reassinar.sh` simulado de ponta a ponta, com dublês do `curl`, `codesign`, `plutil` e `xcodebuild`: caminho normal,
  `.sha256` publicado diferente, secret faltando e envio falhando (a chave `.p8` é apagada nos dois casos).
- shellcheck sem avisos no script; YAML do workflow válido.
- Não dá para conferir fora do Mac da GitHub e do iPhone: a assinatura de verdade, o envio à Apple e o app rodando.

---

SpeedBreaker: Carbon é um projeto de fã (project(u), GPL-3.0), sem relação com a Electronic Arts.
*Need for Speed* e *Carbon* são marcas da EA. Este repositório não contém nem distribui arquivos do jogo.
