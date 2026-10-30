/*
 * Testes do núcleo dos controles por toque (Toque/nucleo.c). Rodam em qualquer computador:
 *   cc -std=c11 -Wall -Wextra -I Toque Toque/nucleo.c testes/testar_nucleo.c -lm -o /tmp/t && /tmp/t
 */
#include "nucleo.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

static int falhas = 0, checagens = 0;
#define CHECK(cond, ...)                                                                                              \
    do {                                                                                                              \
        checagens++;                                                                                                  \
        if (!(cond)) {                                                                                                \
            falhas++;                                                                                                 \
            printf("  FALHOU (linha %d): ", __LINE__);                                                                \
            printf(__VA_ARGS__);                                                                                      \
            printf("\n");                                                                                             \
        }                                                                                                             \
    } while (0)
#define PERTO(a, b) (fabsf((float)(a) - (float)(b)) < 0.01f)

/* iPhone 17 deitado: 874 x 402 pontos; as margens que o Diagnóstico mostrou (62 / 0 / 62 / 20) */
#define W 874.0f
#define H 402.0f

static tq_nucleo n;
static tq_estado_controle s;
static float tela_w = W, tela_h = H;

static void novo_em(void (*mudar)(tq_ajustes *), float w, float h, float l, float t, float r, float b)
{
    tq_ajustes aj;
    tq_ajustes_padrao(&aj);
    if (mudar)
        mudar(&aj);
    tq_iniciar(&n, &aj);
    tela_w = w;
    tela_h = h;
    tq_layout(&n, w, h, l, t, r, b);
    tq_eventos(&n);
}
static void novo(void (*mudar)(tq_ajustes *)) { novo_em(mudar, W, H, 62, 0, 62, 20); }
static void pilotar(void)
{
    tq_definir_modo(&n, TQ_MODO_PILOTAGEM);
    tq_eventos(&n);
}

static void tick(double t) { tq_tick(&n, t, &s); }
static int botao(int o) { return (s.botoes >> o) & 1u; }
static void tocar(uintptr_t id, int c, double t) { tq_toque_comecou(&n, id, n.c[c].cx, n.c[c].cy, t); }
static void mover_para(uintptr_t id, int c, double t) { tq_toque_moveu(&n, id, n.c[c].cx, n.c[c].cy, t); }
static void soltar(uintptr_t id, double t) { tq_toque_acabou(&n, id, t); }
/* toca e solta logo; o tick 50 ms depois mostra o que foi apertado */
static void toque_rapido(uintptr_t id, int c, double t)
{
    tocar(id, c, t);
    soltar(id, t + 0.02);
    tick(t + 0.05);
}

/* A caixa do controle em pontos: x e y do canto de cima à esquerda, largura e altura. */
static int caixa(int c, float x, float y, float w, float h)
{
    const tq_info_controle *k = &n.c[c];
    return k->visivel && PERTO(k->cx - k->meia_l, x) && PERTO(k->cy - k->meia_a, y) && PERTO(2 * k->meia_l, w) &&
           PERTO(2 * k->meia_a, h);
}

/* A distância entre as bordas de dois controles (negativa: se sobrepõem). Retângulos como retângulos cheios. */
static float folga_entre(const tq_info_controle *a, const tq_info_controle *b)
{
    const float dx = fabsf(a->cx - b->cx), dy = fabsf(a->cy - b->cy);
    if (a->forma == TQ_FORMA_CIRCULO && b->forma == TQ_FORMA_CIRCULO)
        return hypotf(dx, dy) - a->meia_l - b->meia_l;
    if (a->forma == TQ_FORMA_RETANGULO && b->forma == TQ_FORMA_RETANGULO) {
        const float gx = dx - a->meia_l - b->meia_l, gy = dy - a->meia_a - b->meia_a;
        if (gx < 0 && gy < 0)
            return gx > gy ? gx : gy;
        return hypotf(gx > 0 ? gx : 0, gy > 0 ? gy : 0);
    }
    const tq_info_controle *c = a->forma == TQ_FORMA_CIRCULO ? a : b, *r = a->forma == TQ_FORMA_CIRCULO ? b : a;
    const float gx = dx - r->meia_l, gy = dy - r->meia_a; /* do centro do círculo ao retângulo */
    if (gx < 0 && gy < 0)
        return (gx > gy ? gx : gy) - c->meia_l;
    return hypotf(gx > 0 ? gx : 0, gy > 0 ? gy : 0) - c->meia_l;
}

/* Tudo dentro das margens, sem encostar um no outro (4 pontos no tamanho 100% no mínimo, o vão do direcional do
 * NFSU2), cada controle acertado no próprio centro, e o meio da tela livre para o jogo. */
static void conferir_tela(const char *nome, float ml, float mt, float mr, float mb)
{
    const float l = fmaxf(12, ml), t = fmaxf(12, mt), r = fmaxf(12, mr), b = fmaxf(12, mb);
    int vistos = 0;
    for (int i = 0; i < TQ_C_COUNT; i++) {
        const tq_info_controle *a = &n.c[i];
        if (!a->visivel)
            continue;
        vistos++;
        CHECK(a->cx - a->meia_l >= l - 0.01f && a->cx + a->meia_l <= tela_w - r + 0.01f &&
                  a->cy - a->meia_a >= t - 0.01f && a->cy + a->meia_a <= tela_h - b + 0.01f,
              "%s: %s sai das margens (%.1f,%.1f %.1fx%.1f)", nome, tq_id_controle(i), a->cx - a->meia_l,
              a->cy - a->meia_a, 2 * a->meia_l, 2 * a->meia_a);
        CHECK(tq_controle_em(&n, a->cx, a->cy) == i, "%s: o centro de %s acerta %s", nome, tq_id_controle(i),
              tq_id_controle(tq_controle_em(&n, a->cx, a->cy)));
        CHECK(a->letra > 0 && a->tam_icone > 0 && a->traco > 0 && a->rotulo && (a->rotulo[0] || a->icone ||
              i == TQ_C_ANALOGICO), "%s: %s tem rótulo ou ícone", nome, tq_id_controle(i));
        for (int j = i + 1; j < TQ_C_COUNT; j++) {
            const tq_info_controle *c = &n.c[j];
            if (!c->visivel)
                continue;
            const float f = folga_entre(a, c);
            CHECK(f >= 4 * n.s - 0.01f, "%s: %s e %s a %.1f pontos", nome, tq_id_controle(i), tq_id_controle(j), f);
        }
    }
    CHECK(vistos >= 9, "%s: só %d controles", nome, vistos);
    CHECK(tq_controle_em(&n, tela_w / 2, tela_h / 2) == -1, "%s: o meio da tela é do jogo", nome);
    CHECK(tq_controle_em(&n, tela_w * 0.6f, tela_h * 0.45f) == -1, "%s: o meio à direita é do jogo", nome);
}

static void escala_08(tq_ajustes *a) { a->escala = 0.8f; }
static void escala_15(tq_ajustes *a) { a->escala = 1.5f; }
static void analogico(tq_ajustes *a) { a->direcao = TQ_DIRECAO_ANALOGICO; }
static void inclinacao(tq_ajustes *a) { a->direcao = TQ_DIRECAO_INCLINACAO; }
static void manual(tq_ajustes *a) { a->cambio_manual = 1; }
static void analogico_manual(tq_ajustes *a) { a->direcao = TQ_DIRECAO_ANALOGICO; a->cambio_manual = 1; }
static void inclinacao_manual(tq_ajustes *a) { a->direcao = TQ_DIRECAO_INCLINACAO; a->cambio_manual = 1; }
static void tudo_15(tq_ajustes *a) { a->escala = 1.5f; a->cambio_manual = 1; a->direcao = TQ_DIRECAO_INCLINACAO; }
static void tudo_15b(tq_ajustes *a) { a->escala = 1.5f; a->cambio_manual = 1; a->direcao = TQ_DIRECAO_ANALOGICO; }
static void tudo_08(tq_ajustes *a) { a->escala = 0.8f; a->cambio_manual = 1; }
static void sem_pausa_menu(tq_ajustes *a) { a->pausa_vai_para_menu = 0; }
static void freio_mao_no_x(tq_ajustes *a) { a->mapa[TQ_ACAO_FREIO_MAO] = TQ_O_X; }
static void ajustes_ruins(tq_ajustes *a) { a->escala = 9; a->direcao = 7; a->mapa[0] = 200; }

int main(void)
{
    printf("1. posições: dentro das margens, sem encostar, cada um acertado no próprio centro\n");
    {
        void (*variantes[])(tq_ajustes *) = { NULL, escala_08, escala_15, analogico, inclinacao, manual,
                                              analogico_manual, inclinacao_manual, tudo_15, tudo_15b, tudo_08 };
        const struct { float w, h, l, t, r, b; const char *nome; } telas[] = {
            { 874, 402, 62, 0, 62, 20, "iPhone 17" },        { 874, 402, 59, 0, 59, 21, "iPhone 17 (59)" },
            { 956, 440, 62, 0, 62, 20, "iPhone 17 Pro Max" }, { 667, 375, 0, 0, 0, 0, "iPhone SE" },
            { 1180, 820, 0, 24, 0, 20, "iPad Air" },          { 1024, 768, 0, 20, 0, 20, "iPad 4:3" },
        };
        for (unsigned tl = 0; tl < sizeof telas / sizeof *telas; tl++)
            for (unsigned v = 0; v < sizeof variantes / sizeof *variantes; v++)
                for (int modo = 0; modo < 2; modo++) {
                    novo_em(variantes[v], telas[tl].w, telas[tl].h, telas[tl].l, telas[tl].t, telas[tl].r,
                            telas[tl].b);
                    tq_definir_modo(&n, modo);
                    char nome[96];
                    snprintf(nome, sizeof nome, "%s, variante %u, %s", telas[tl].nome, v, modo ? "pilotar" : "menu");
                    conferir_tela(nome, telas[tl].l, telas[tl].t, telas[tl].r, telas[tl].b);
                }
    }

    printf("2. as medidas do NFSU2 no iPhone 17 (tamanho 100%%, margens 62 / 12 / 62 / 20)\n");
    novo(NULL);
    CHECK(PERTO(n.s, 1), "tamanho 100%% cabe (s=%.3f)", n.s);
    CHECK(caixa(TQ_C_AJUSTES, 62, 12, 48, 48) && n.c[TQ_C_AJUSTES].forma == TQ_FORMA_CIRCULO, "≡ no canto");
    /* menu: teclas de 44 com vão de 4, Voltar sobre OK com 99 de largura (a faixa ao lado de um menu 4:3) */
    CHECK(caixa(TQ_C_DCIMA, 110, 242, 44, 44) && caixa(TQ_C_DESQ, 62, 290, 44, 44) &&
              caixa(TQ_C_DDIR, 158, 290, 44, 44) && caixa(TQ_C_DBAIXO, 110, 338, 44, 44),
          "direcional em cruz");
    CHECK(PERTO(n.c[TQ_C_DCIMA].canto, 12) && n.c[TQ_C_DCIMA].icone == TQ_ICONE_CIMA &&
              n.c[TQ_C_DBAIXO].icone == TQ_ICONE_BAIXO && n.c[TQ_C_DESQ].icone == TQ_ICONE_ESQUERDA,
          "cantos e ícones do direcional");
    CHECK(caixa(TQ_C_VOLTAR, 713, 282, 99, 44) && caixa(TQ_C_OK, 713, 338, 99, 44) && PERTO(n.c[TQ_C_OK].canto, 22),
          "Voltar e OK (%.1f,%.1f %.1fx%.1f)", n.c[TQ_C_OK].cx - n.c[TQ_C_OK].meia_l,
          n.c[TQ_C_OK].cy - n.c[TQ_C_OK].meia_a, 2 * n.c[TQ_C_OK].meia_l, 2 * n.c[TQ_C_OK].meia_a);
    CHECK(caixa(TQ_C_X, 713, 226, 44, 44) && caixa(TQ_C_Y, 768, 226, 44, 44), "X e Y sobre o Voltar");
    CHECK(caixa(TQ_C_LB, 118, 16, 60, 40) && caixa(TQ_C_RB, 752, 16, 60, 40), "LB e RB em cima");
    CHECK(caixa(TQ_C_MODO, 385, 16, 104, 40) && caixa(TQ_C_BACK, 305, 16, 72, 40) && caixa(TQ_C_START, 497, 16, 72, 40),
          "Pilotar no meio, Back e Start dos lados");
    CHECK(strcmp(n.c[TQ_C_MODO].rotulo, "Pilotar") == 0 && strcmp(n.c[TQ_C_OK].rotulo, "OK") == 0, "rótulos do menu");
    {
        int com_borda = 1;
        for (int c = 0; c < TQ_C_COUNT; c++)
            if (n.c[c].visivel && c != TQ_C_AJUSTES && !n.c[c].borda)
                com_borda = 0;
        CHECK(com_borda && !n.c[TQ_C_AJUSTES].borda, "o aro fino nos do menu (não no ≡)");
    }
    pilotar();
    /* a fileira de cima, de 56 em 56 */
    const int fileira[] = { TQ_C_AJUSTES, TQ_C_PAUSA, TQ_C_CAMERA, TQ_C_RESET, TQ_C_MAPA, TQ_C_SMS, TQ_C_PARCEIRO };
    for (int i = 0; i < 7; i++)
        CHECK(caixa(fileira[i], 62 + 56 * i, 12, 48, 48) && n.c[fileira[i]].forma == TQ_FORMA_CIRCULO,
              "fileira: %s em %d", tq_id_controle(fileira[i]), 62 + 56 * i);
    CHECK(n.c[TQ_C_PAUSA].icone == TQ_ICONE_PAUSA && n.c[TQ_C_CAMERA].icone == TQ_ICONE_CAMERA &&
              n.c[TQ_C_RESET].icone == TQ_ICONE_RESET && n.c[TQ_C_MAPA].icone == TQ_ICONE_MAPA &&
              n.c[TQ_C_SMS].icone == TQ_ICONE_SMS && n.c[TQ_C_AJUSTES].icone == TQ_ICONE_MENU,
          "ícones da fileira");
    CHECK(strcmp(n.c[TQ_C_PARCEIRO].rotulo, "Equipe") == 0 && PERTO(n.c[TQ_C_PARCEIRO].letra, 12), "Equipe, letra 12");
    CHECK(caixa(TQ_C_DIRECAO, 764, 12, 48, 48) && n.c[TQ_C_DIRECAO].icone == TQ_ICONE_BOTOES, "jeito de dirigir");
    CHECK(caixa(TQ_C_MODO, 668, 16, 88, 40) && strcmp(n.c[TQ_C_MODO].rotulo, "Menu") == 0, "Menu ao lado");
    CHECK(!n.c[TQ_C_RETO].visivel && !n.c[TQ_C_ANALOGICO].visivel && !n.c[TQ_C_SOBE].visivel, "escondidos");
    CHECK(caixa(TQ_C_ESQUERDA, 62, 278, 80, 104) && caixa(TQ_C_DIREITA, 150, 278, 80, 104) &&
              PERTO(n.c[TQ_C_ESQUERDA].canto, 16) && PERTO(n.c[TQ_C_ESQUERDA].tam_icone, 32),
          "◀ e ▶ de 80 x 104");
    CHECK(caixa(TQ_C_ENTRAR, 62, 214, 92, 40) && PERTO(n.c[TQ_C_ENTRAR].canto, 20), "Entrar acima das setas");
    CHECK(caixa(TQ_C_GAS, 728, 234, 84, 148) && caixa(TQ_C_FREIO, 636, 278, 84, 104), "Gás e Freio");
    CHECK(caixa(TQ_C_NITRO, 740, 162, 60, 60) && n.c[TQ_C_NITRO].forma == TQ_FORMA_CIRCULO &&
              PERTO(n.c[TQ_C_NITRO].letra, 14),
          "Nitro redondo de 60 sobre o Gás");
    CHECK(caixa(TQ_C_FREIO_MAO, 650, 210, 56, 56) && PERTO(n.c[TQ_C_FREIO_MAO].letra, 12) &&
              strcmp(n.c[TQ_C_FREIO_MAO].rotulo, "Freio\nde m\xC3\xA3o") == 0,
          "Freio de mão sobre o Freio");
    CHECK(caixa(TQ_C_SPEEDBREAKER, 572, 302, 56, 56) && strcmp(n.c[TQ_C_SPEEDBREAKER].rotulo, "Speed\nbreaker") == 0,
          "Speedbreaker à esquerda do Freio");
    CHECK(strcmp(n.c[TQ_C_GAS].rotulo, "G\xC3\xA1s") == 0 && strcmp(n.c[TQ_C_NITRO].rotulo, "N\xE2\x82\x82O") == 0,
          "Gás e N₂O");
    novo(manual);
    pilotar();
    CHECK(caixa(TQ_C_DESCE, 62, 214, 80, 56) && caixa(TQ_C_SOBE, 150, 214, 80, 56) && PERTO(n.c[TQ_C_SOBE].traco, 2.5f) &&
              n.c[TQ_C_SOBE].icone == TQ_ICONE_MARCHA_CIMA && n.c[TQ_C_DESCE].icone == TQ_ICONE_MARCHA_BAIXO,
          "marchas acima das setas");
    CHECK(caixa(TQ_C_ENTRAR, 62, 158, 92, 40), "Entrar acima das marchas");
    novo(analogico_manual);
    pilotar();
    CHECK(caixa(TQ_C_ANALOGICO, 62, 262, 120, 120) && n.c[TQ_C_ANALOGICO].forma == TQ_FORMA_CIRCULO, "base do analógico");
    CHECK(caixa(TQ_C_DESCE, 62, 194, 80, 56) && caixa(TQ_C_ENTRAR, 62, 138, 92, 40), "marchas acima do analógico");
    CHECK(n.c[TQ_C_DIRECAO].icone == TQ_ICONE_ANALOGICO && PERTO(n.trava, 64), "ícone e fim de curso do analógico");
    CHECK(PERTO(n.zona_x0, 0) && PERTO(n.zona_x1, W * 0.42f) && PERTO(n.zona_y0, H * 0.28f) && PERTO(n.zona_y1, H),
          "área do polegar: 42%% x 72%% embaixo à esquerda");
    novo(inclinacao);
    pilotar();
    CHECK(caixa(TQ_C_RETO, 708, 12, 48, 48) && n.c[TQ_C_RETO].icone == TQ_ICONE_RETO, "Reto ao lado");
    CHECK(caixa(TQ_C_MODO, 612, 16, 88, 40) && n.c[TQ_C_DIRECAO].icone == TQ_ICONE_INCLINACAO, "Menu antes do Reto");
    CHECK(!n.c[TQ_C_ESQUERDA].visivel && caixa(TQ_C_FREIO, 62, 278, 84, 104) && caixa(TQ_C_FREIO_MAO, 158, 302, 56, 56),
          "Freio e Freio de mão sob o polegar esquerdo");
    CHECK(caixa(TQ_C_NITRO, 656, 298, 60, 60) && caixa(TQ_C_SPEEDBREAKER, 742, 164, 56, 56) &&
              caixa(TQ_C_GAS, 728, 234, 84, 148),
          "Nitro ao lado do Gás, Speedbreaker sobre ele");
    novo(escala_15);
    CHECK(PERTO(n.s, 370.0f / 324), "150%% limitado ao que cabe (s=%.3f)", n.s);
    pilotar();
    CHECK(PERTO(n.c[TQ_C_GAS].meia_a * 2, 148 * n.s) && PERTO(n.c[TQ_C_GAS].letra, 16 * n.s), "tudo no tamanho");
    novo(escala_08);
    pilotar();
    CHECK(PERTO(n.s, 0.8f) && caixa(TQ_C_GAS, W - 62 - 84 * 0.8f, H - 20 - 148 * 0.8f, 84 * 0.8f, 148 * 0.8f),
          "80%% encosta nas mesmas margens");

    printf("3. um toque rápido segura o botão 150 ms (NFSU2: HOLD_MS)\n");
    novo(NULL);
    CHECK(n.modo == TQ_MODO_MENU, "começa no modo Menu");
    tocar(1, TQ_C_OK, 1.00);
    soltar(1, 1.01);
    tick(1.05);
    CHECK(botao(TQ_O_A), "A apertado logo depois do toque");
    tick(1.145);
    CHECK(botao(TQ_O_A), "A ainda apertado aos 145 ms");
    tick(1.155);
    CHECK(!botao(TQ_O_A), "A solto depois de 150 ms");
    tocar(1, TQ_C_OK, 1.2);
    tick(1.6);
    CHECK(botao(TQ_O_A), "segurando: continua apertado");
    soltar(1, 1.61);
    tick(1.62);
    CHECK(!botao(TQ_O_A), "segurou mais que 150 ms: solta junto");

    printf("4. menu: cada controle aperta o seu botão\n");
    {
        const int c[] = { TQ_C_DCIMA, TQ_C_DBAIXO, TQ_C_DESQ, TQ_C_DDIR, TQ_C_OK, TQ_C_VOLTAR,
                          TQ_C_X, TQ_C_Y, TQ_C_LB, TQ_C_RB, TQ_C_START, TQ_C_BACK };
        const int o[] = { TQ_O_DUP, TQ_O_DDOWN, TQ_O_DLEFT, TQ_O_DRIGHT, TQ_O_A, TQ_O_B,
                          TQ_O_X, TQ_O_Y, TQ_O_LB, TQ_O_RB, TQ_O_START, TQ_O_BACK };
        double t = 2;
        for (int i = 0; i < 12; i++, t += 1) {
            toque_rapido(2, c[i], t);
            CHECK(s.botoes == (1u << o[i]) && s.lt == 0 && s.rt == 0 && s.lx == 0, "%s aperta só %s",
                  tq_id_controle(c[i]), tq_nome_saida(o[i]));
        }
        /* o direcional não desliza (NFSU2): o dedo fica na tecla em que encostou */
        tocar(3, TQ_C_DCIMA, 20.0);
        mover_para(3, TQ_C_DDIR, 20.3);
        tick(20.35);
        CHECK(botao(TQ_O_DUP) && !botao(TQ_O_DRIGHT), "arrastar do ↑ para o →: continua o ↑");
        soltar(3, 20.4);
        tick(20.41);
        CHECK(s.botoes == 0, "soltou: nada");
    }

    printf("5. pilotagem: os pedais deslizam (Gás, Nitro, Freio); fora deles, fica no último\n");
    novo(NULL);
    tq_definir_modo(&n, TQ_MODO_PILOTAGEM);
    CHECK(tq_eventos(&n) & TQ_EV_MODO, "evento de modo");
    CHECK(!n.c[TQ_C_DCIMA].visivel && !n.c[TQ_C_OK].visivel && n.c[TQ_C_GAS].visivel, "visíveis da pilotagem");
    tocar(3, TQ_C_GAS, 3.0);
    tick(3.05);
    CHECK(s.rt == 1 && s.lt == 0 && s.botoes == 0, "Gás = RT");
    CHECK(n.c[TQ_C_GAS].apertado, "Gás aceso");
    tq_eventos(&n);
    mover_para(3, TQ_C_NITRO, 3.3);
    CHECK(tq_eventos(&n) & TQ_EV_TOQUE, "vibra ao entrar no Nitro");
    tick(3.35);
    CHECK(s.rt == 1 && botao(TQ_O_B) && n.c[TQ_C_NITRO].apertado && !n.c[TQ_C_GAS].apertado,
          "Nitro = B e mantém o gás");
    tick(3.5);
    CHECK(s.rt == 1 && botao(TQ_O_B), "Nitro segurado continua com gás");
    mover_para(3, TQ_C_FREIO, 3.6);
    tick(3.8);
    CHECK(s.lt == 1 && s.rt == 0 && !botao(TQ_O_B), "Freio = LT, sem gás nem nitro");
    tq_toque_moveu(&n, 3, W / 2, H / 2, 3.9);
    tick(4.0);
    CHECK(s.lt == 1, "fora dos pedais, fica no último");
    mover_para(3, TQ_C_FREIO_MAO, 4.05);
    tick(4.06);
    CHECK(s.lt == 1 && !botao(TQ_O_A), "o Freio de mão não é do grupo dos pedais");
    soltar(3, 4.1);
    tick(4.3);
    CHECK(s.lt == 0 && s.rt == 0, "soltou: nada");
    tocar(3, TQ_C_FREIO, 4.4);
    mover_para(3, TQ_C_GAS, 4.42);
    tick(4.45);
    CHECK(s.rt == 1 && s.lt == 1, "Freio para Gás: o Freio segura o aperto mínimo");
    tick(4.56);
    CHECK(s.rt == 1 && s.lt == 0, "e depois só o Gás");
    soltar(3, 4.6);

    printf("6. direção por botões, com rampa\n");
    tick(4.99);
    tocar(4, TQ_C_DIREITA, 5.0);
    double t = 4.99;
    for (int i = 0; i < 5; i++)
        tick(t += 1.0 / 60);
    CHECK(s.lx > 0.35f && s.lx < 0.6f, "metade do caminho em ~0,08 s (lx=%.2f)", s.lx);
    for (int i = 0; i < 10; i++)
        tick(t += 1.0 / 60);
    CHECK(s.lx == 1.0f, "fim de curso (lx=%.2f)", s.lx);
    mover_para(4, TQ_C_ESQUERDA, t);
    for (int i = 0; i < 3; i++)
        tick(t += 1.0 / 60);
    CHECK(s.lx < 0.5f, "deslizou para a esquerda: voltando (lx=%.2f)", s.lx);
    CHECK(n.c[TQ_C_ESQUERDA].apertado && !n.c[TQ_C_DIREITA].apertado, "a seta acesa é a da esquerda");
    for (int i = 0; i < 20; i++)
        tick(t += 1.0 / 60);
    CHECK(s.lx == -1.0f, "fim de curso à esquerda (lx=%.2f)", s.lx);
    tq_toque_moveu(&n, 4, W / 2, H / 2, t);
    tick(t += 1.0 / 60);
    CHECK(s.lx == -1.0f, "fora das setas, fica na última");
    soltar(4, t);
    for (int i = 0; i < 6; i++)
        tick(t += 1.0 / 60);
    CHECK(s.lx == 0.0f, "soltou: volta ao centro em ~0,08 s (lx=%.2f)", s.lx);

    printf("7. analógico (NFSU2: a base vem para debaixo do polegar, 64 pontos até o fim de curso)\n");
    novo(analogico);
    pilotar();
    {
        const float x = 62 + 30, y = H - 80;
        CHECK(tq_controle_em(&n, x, y) == TQ_C_ANALOGICO, "a área do polegar é do analógico");
        CHECK(tq_controle_em(&n, n.c[TQ_C_ENTRAR].cx, n.c[TQ_C_ENTRAR].cy) == TQ_C_ENTRAR, "o Entrar ganha da área");
        tq_toque_comecou(&n, 5, x, y, 6.0);
        CHECK(n.analogico_ativo && PERTO(n.base_x, x) && PERTO(n.base_y, y), "a base veio para o polegar");
        tq_toque_moveu(&n, 5, x + 64, y + 30, 6.1);
        tick(6.1);
        CHECK(fabsf(s.lx - 1) < 1e-4f && s.ly == 0, "64 pontos à direita = fim de curso (lx=%.3f)", s.lx);
        CHECK(PERTO(n.knob_x, x + 64) && PERTO(n.knob_y, y), "o polegar só anda para os lados");
        tq_toque_moveu(&n, 5, x - 32, y, 6.2);
        tick(6.2);
        CHECK(fabsf(s.lx + 0.5f) < 1e-3f, "32 à esquerda = metade (lx=%.3f)", s.lx);
        tq_toque_moveu(&n, 5, x - 300, y, 6.25);
        tick(6.25);
        CHECK(s.lx == -1 && PERTO(n.knob_x, x - 64), "passou do fim: fica no fim");
        CHECK(n.c[TQ_C_ANALOGICO].apertado, "aceso");
        soltar(5, 6.3);
        tick(6.3);
        CHECK(s.lx == 0 && !n.analogico_ativo, "soltou: centro");
        CHECK(PERTO(n.base_x, n.c[TQ_C_ANALOGICO].cx) && PERTO(n.base_y, n.c[TQ_C_ANALOGICO].cy), "a base voltou ao canto");
        CHECK(tq_controle_em(&n, n.c[TQ_C_GAS].cx, n.c[TQ_C_GAS].cy) == TQ_C_GAS, "o Gás continua sendo o Gás");
    }
    novo(escala_08);
    tq_ir_para_tela(&n, TQ_TELA_ANALOGICO);
    {
        const float x = 100, y = H - 60;
        tq_toque_comecou(&n, 5, x, y, 6.5);
        tq_toque_moveu(&n, 5, x + 64 * 0.8f, y, 6.6);
        tick(6.6);
        CHECK(fabsf(s.lx - 1) < 1e-4f, "no tamanho 80%%, o fim de curso é 51,2 (lx=%.3f)", s.lx);
        soltar(5, 6.7);
    }

    printf("8. inclinação (NFSU2: 30 graus até o fim, sem zona morta, suavizada)\n");
    CHECK(fabsf(tq_inclinacao_relativa(170, -170) + 20) < 1e-4f, "passa de 180 para -180");
    CHECK(fabsf(tq_inclinacao_relativa(-170, 170) - 20) < 1e-4f, "passa de -180 para 180");
    novo(inclinacao);
    pilotar();
    tq_definir_inclinacao(&n, 15);
    t = 7.0;
    tick(t);
    tick(t += 1.0 / 60);
    CHECK(fabsf(s.lx - 0.3f) < 1e-4f, "a primeira leitura: 60%% do caminho (lx=%.4f)", s.lx);
    for (int i = 0; i < 20; i++)
        tick(t += 1.0 / 60);
    CHECK(s.lx == 0.5f, "15 graus = metade (lx=%.4f)", s.lx);
    tq_definir_inclinacao(&n, 1);
    for (int i = 0; i < 30; i++)
        tick(t += 1.0 / 60);
    CHECK(fabsf(s.lx - 1.0f / 30) < 1e-4f, "1 grau já vira (lx=%.4f)", s.lx);
    tq_definir_inclinacao(&n, -45);
    for (int i = 0; i < 30; i++)
        tick(t += 1.0 / 60);
    CHECK(s.lx == -1, "45 graus = fim de curso");
    {
        /* a 120 quadros por segundo, a mesma suavização */
        novo(inclinacao);
        pilotar();
        tq_definir_inclinacao(&n, 30);
        tick(8.0);
        tick(8.0 + 1.0 / 120);
        tick(8.0 + 2.0 / 120);
        CHECK(fabsf(s.lx - 0.6f) < 1e-3f, "a 120 Hz: o mesmo 60%% em 1/60 s (lx=%.4f)", s.lx);
    }
    tocar(6, TQ_C_RETO, 7.3);
    CHECK(!(tq_eventos(&n) & TQ_EV_RETO), "o Reto não age ao encostar");
    soltar(6, 7.35);
    CHECK(tq_eventos(&n) & TQ_EV_RETO, "o Reto pede recalibrar ao soltar");

    printf("9. câmera: toque troca (RB), segurar olha para trás (RS)\n");
    novo(NULL);
    pilotar();
    tocar(7, TQ_C_CAMERA, 8.0);
    tick(8.05);
    CHECK(!botao(TQ_O_RB) && !botao(TQ_O_RS), "nada enquanto não decide");
    CHECK(n.c[TQ_C_CAMERA].apertado, "acesa");
    soltar(7, 8.10);
    tick(8.12);
    CHECK(botao(TQ_O_RB), "toque rápido: RB");
    tick(8.24);
    CHECK(botao(TQ_O_RB), "RB segura 150 ms");
    tick(8.26);
    CHECK(!botao(TQ_O_RB), "RB solto");
    tocar(7, TQ_C_CAMERA, 9.0);
    tick(9.24);
    CHECK(!botao(TQ_O_RS), "antes de 0,25 s: nada");
    tick(9.3);
    CHECK(botao(TQ_O_RS) && !botao(TQ_O_RB), "segurando: RS");
    soltar(7, 9.5);
    tick(9.51);
    CHECK(!botao(TQ_O_RS) && !botao(TQ_O_RB), "soltou depois de segurar: nem RS nem RB");

    printf("10. os outros da pilotagem, com o mapa padrão\n");
    {
        const int c[] = { TQ_C_FREIO_MAO, TQ_C_SPEEDBREAKER, TQ_C_PARCEIRO, TQ_C_RESET, TQ_C_ENTRAR, TQ_C_MAPA, TQ_C_SMS };
        const int o[] = { TQ_O_A, TQ_O_X, TQ_O_Y, TQ_O_LB, TQ_O_BACK, TQ_O_DUP, TQ_O_DDOWN };
        t = 30;
        for (int i = 0; i < 7; i++, t += 1) {
            toque_rapido(2, c[i], t);
            CHECK(s.botoes == (1u << o[i]) && s.lt == 0 && s.rt == 0, "%s aperta %s", tq_id_controle(c[i]),
                  tq_nome_saida(o[i]));
        }
        toque_rapido(2, TQ_C_NITRO, t);
        CHECK(s.botoes == (1u << TQ_O_B) && s.rt == 1, "um toque no Nitro: B e gás, os dois o aperto mínimo");
    }

    printf("11. pausa: Start, e ao soltar muda para o Menu\n");
    tocar(8, TQ_C_PAUSA, 10.0);
    tick(10.05);
    CHECK(botao(TQ_O_START) && n.modo == TQ_MODO_PILOTAGEM, "Start apertado");
    soltar(8, 10.1);
    CHECK(n.modo == TQ_MODO_MENU, "foi para o Menu");
    CHECK(tq_eventos(&n) & TQ_EV_MODO, "evento de modo");
    tick(10.12);
    CHECK(botao(TQ_O_START), "o Start segura o aperto mínimo mesmo no Menu");
    tick(10.16);
    CHECK(!botao(TQ_O_START), "e solta");
    novo(sem_pausa_menu);
    pilotar();
    tocar(8, TQ_C_PAUSA, 11.0);
    soltar(8, 11.1);
    CHECK(n.modo == TQ_MODO_PILOTAGEM, "com o ajuste desligado, fica na pilotagem");

    printf("12. Menu e Pilotar: troca ao soltar; o que ficou apertado do outro modo não vale\n");
    novo(NULL);
    tocar(9, TQ_C_MODO, 12.0);
    CHECK(n.modo == TQ_MODO_MENU, "não troca ao encostar");
    soltar(9, 12.05);
    CHECK(n.modo == TQ_MODO_PILOTAGEM && strcmp(n.c[TQ_C_MODO].rotulo, "Menu") == 0, "Pilotar troca o modo");
    tocar(10, TQ_C_GAS, 12.1);
    tocar(9, TQ_C_MODO, 12.2);
    soltar(9, 12.25);
    tick(12.4);
    CHECK(n.modo == TQ_MODO_MENU && s.rt == 0, "no menu, o gás que ficou apertado não vale");
    soltar(10, 12.5);

    printf("13. o botão de direção passa por botões, analógico e inclinação (ao soltar)\n");
    novo(NULL);
    pilotar();
    tick(13.0);
    tocar(20, TQ_C_DIREITA, 13.0);
    for (t = 13.0; t < 13.3;)
        tick(t += 1.0 / 60);
    CHECK(s.lx == 1, "virando");
    tocar(21, TQ_C_DIRECAO, 13.3);
    CHECK(n.aj.direcao == TQ_DIRECAO_BOTOES && !(tq_eventos(&n) & TQ_EV_DIRECAO), "não troca ao encostar");
    soltar(21, 13.35);
    {
        const uint32_t e = tq_eventos(&n);
        CHECK((e & TQ_EV_DIRECAO) && (e & TQ_EV_LAYOUT), "eventos de direção e de posições");
    }
    CHECK(n.aj.direcao == TQ_DIRECAO_ANALOGICO && tq_tela_atual(&n) == TQ_TELA_ANALOGICO, "agora o analógico");
    CHECK(!n.c[TQ_C_ESQUERDA].visivel && n.c[TQ_C_ANALOGICO].visivel && n.c[TQ_C_DIRECAO].icone == TQ_ICONE_ANALOGICO,
          "as setas saem, a base entra");
    tick(13.4);
    CHECK(s.lx == 0, "o que a seta apertava foi solto");
    tq_toque_moveu(&n, 20, 100, H - 50, 13.45);
    tick(13.5);
    CHECK(s.lx == 0 && !n.analogico_ativo, "o dedo da seta não vira analógico");
    soltar(20, 13.5);
    tocar(22, TQ_C_GAS, 13.55);
    toque_rapido(21, TQ_C_DIRECAO, 13.6);
    tick(13.75);
    CHECK(s.rt == 0, "o pedal também é solto (NFSU2: o Freio muda de lado)");
    soltar(22, 13.76);
    CHECK(n.aj.direcao == TQ_DIRECAO_INCLINACAO && n.c[TQ_C_RETO].visivel && n.c[TQ_C_FREIO].cx < W / 2,
          "inclinação: o Reto aparece e o Freio vai para a esquerda");
    toque_rapido(21, TQ_C_DIRECAO, 13.8);
    CHECK(n.aj.direcao == TQ_DIRECAO_BOTOES && n.c[TQ_C_ESQUERDA].visivel && !n.c[TQ_C_RETO].visivel, "e de volta");

    printf("14. ajustes, Back + Start, reatribuir\n");
    tocar(13, TQ_C_AJUSTES, 14.0);
    CHECK(!(tq_eventos(&n) & TQ_EV_AJUSTES), "não abre ao encostar");
    soltar(13, 14.05);
    CHECK(tq_eventos(&n) & TQ_EV_AJUSTES, "abre ao soltar");
    tq_pulso(&n, (1u << TQ_O_BACK) | (1u << TQ_O_START), 14.1, 0.25);
    tick(14.2);
    CHECK(botao(TQ_O_BACK) && botao(TQ_O_START), "Back + Start juntos");
    tick(14.4);
    CHECK(!botao(TQ_O_BACK) && !botao(TQ_O_START), "e soltos depois");
    novo(freio_mao_no_x);
    pilotar();
    toque_rapido(14, TQ_C_FREIO_MAO, 15.0);
    CHECK(botao(TQ_O_X) && !botao(TQ_O_A), "freio de mão reatribuído para X");

    printf("15. marchas no analógico direito\n");
    novo(manual);
    pilotar();
    toque_rapido(15, TQ_C_SOBE, 16.0);
    CHECK(s.ry == -1 && s.rx == 0, "Marcha + = analógico direito para cima (ry=%.0f)", s.ry);
    tick(16.2);
    toque_rapido(16, TQ_C_DESCE, 16.3);
    CHECK(s.ry == 1, "Marcha - = para baixo (ry=%.0f)", s.ry);

    printf("16. mover os botões: arrastar, sem nada ir para o jogo\n");
    novo(NULL);
    pilotar();
    tq_editar(&n, 1);
    CHECK(n.editando && (tq_eventos(&n) & TQ_EV_LAYOUT), "modo de mover");
    {
        const float gx = n.c[TQ_C_GAS].cx, gy = n.c[TQ_C_GAS].cy;
        tocar(30, TQ_C_GAS, 20.0);
        CHECK(n.selecionado == TQ_C_GAS && (tq_eventos(&n) & TQ_EV_TOQUE), "o Gás selecionado");
        tick(20.05);
        CHECK(s.rt == 0 && s.botoes == 0 && s.lx == 0, "nada vai para o jogo");
        tq_toque_moveu(&n, 30, gx - 100, gy - 50, 20.1);
        CHECK(PERTO(n.c[TQ_C_GAS].cx, gx - 100) && PERTO(n.c[TQ_C_GAS].cy, gy - 50), "o Gás foi junto");
        tq_posicao p = tq_posicao_de(&n, TQ_TELA_BOTOES, TQ_C_GAS);
        CHECK(PERTO(p.dx, -100) && PERTO(p.dy, -50) && p.tamanho == 1, "posição guardada (%.1f, %.1f)", p.dx, p.dy);
        CHECK(PERTO(n.c[TQ_C_FREIO].cx, 678), "o Freio ficou onde estava");
        /* arrastar para fora: fica na borda, e o arrasto seguinte parte da borda */
        tq_toque_moveu(&n, 30, gx + 500, gy, 20.2);
        CHECK(PERTO(n.c[TQ_C_GAS].cx, W - 42), "parou na borda (cx=%.1f)", n.c[TQ_C_GAS].cx);
        soltar(30, 20.3);
        p = tq_posicao_de(&n, TQ_TELA_BOTOES, TQ_C_GAS);
        CHECK(PERTO(p.dx, W - 42 - gx), "guardou a posição da borda (%.1f)", p.dx);
        tq_toque_comecou(&n, 31, W - 42, gy, 20.4);
        tq_toque_moveu(&n, 31, W - 52, gy, 20.5);
        CHECK(PERTO(n.c[TQ_C_GAS].cx, W - 52), "sem pulo ao arrastar de novo (cx=%.1f)", n.c[TQ_C_GAS].cx);
        soltar(31, 20.6);
    }

    {
        /* no tamanho 80%, o controle anda o mesmo que o dedo; o guardado é em pontos do 100% */
        tq_nucleo salvo = n;
        novo(escala_08);
        pilotar();
        tq_editar(&n, 1);
        const float gx = n.c[TQ_C_GAS].cx;
        tocar(39, TQ_C_GAS, 20.7);
        tq_toque_moveu(&n, 39, gx - 80, n.c[TQ_C_GAS].cy, 20.8);
        soltar(39, 20.9);
        CHECK(PERTO(n.c[TQ_C_GAS].cx, gx - 80), "no 80%%, o Gás anda com o dedo (%.1f)", n.c[TQ_C_GAS].cx - gx);
        CHECK(PERTO(tq_posicao_de(&n, TQ_TELA_BOTOES, TQ_C_GAS).dx, -100), "guardado: -100 no 100%%");
        n = salvo;
    }

    printf("17. pinça: o segundo dedo, onde cair, muda o tamanho do controle\n");
    tq_restaurar_tela(&n, TQ_TELA_BOTOES);
    {
        const tq_info_controle k0 = n.c[TQ_C_NITRO];
        tq_toque_comecou(&n, 32, k0.cx, k0.cy, 21.0);
        tq_toque_comecou(&n, 33, k0.cx - 100, k0.cy, 21.0); /* longe do Nitro */
        CHECK(n.selecionado == TQ_C_NITRO, "o segundo dedo foi para o Nitro");
        tq_toque_moveu(&n, 33, k0.cx - 150, k0.cy, 21.1);
        tq_posicao p = tq_posicao_de(&n, TQ_TELA_BOTOES, TQ_C_NITRO);
        CHECK(fabsf(p.tamanho - 1.5f) < 1e-4f, "100 para 150 pontos: 150%% (%.3f)", p.tamanho);
        CHECK(PERTO(n.c[TQ_C_NITRO].meia_l, k0.meia_l * 1.5f) && PERTO(n.c[TQ_C_NITRO].letra, k0.letra * 1.5f) &&
                  PERTO(n.c[TQ_C_NITRO].canto, k0.canto * 1.5f),
              "o desenho cresce junto");
        CHECK(PERTO(n.c[TQ_C_NITRO].cx, k0.cx - 25), "o meio dos dedos andou 25: o centro também (%.1f)",
              n.c[TQ_C_NITRO].cx);
        tq_toque_moveu(&n, 33, k0.cx - 1000, k0.cy, 21.2);
        CHECK(tq_posicao_de(&n, TQ_TELA_BOTOES, TQ_C_NITRO).tamanho == TQ_TAMANHO_MAX, "até 250%%");
        tq_toque_moveu(&n, 33, k0.cx - 2, k0.cy, 21.3);
        CHECK(tq_posicao_de(&n, TQ_TELA_BOTOES, TQ_C_NITRO).tamanho == TQ_TAMANHO_MIN, "até 50%%");
        tq_toque_moveu(&n, 33, k0.cx - 100, k0.cy, 21.4);
        /* tira um dedo: o outro continua arrastando de onde o controle está, sem pulo */
        const float cx = n.c[TQ_C_NITRO].cx, cy = n.c[TQ_C_NITRO].cy;
        soltar(33, 21.5);
        tq_toque_moveu(&n, 32, k0.cx + 5, k0.cy + 7, 21.6);
        CHECK(PERTO(n.c[TQ_C_NITRO].cx, cx + 5) && PERTO(n.c[TQ_C_NITRO].cy, cy + 7), "um dedo só: arrasta de onde está");
        CHECK(fabsf(tq_posicao_de(&n, TQ_TELA_BOTOES, TQ_C_NITRO).tamanho - 1) < 1e-4f, "o tamanho ficou");
        /* volta o segundo dedo: a pinça parte do tamanho de agora */
        tq_toque_comecou(&n, 34, k0.cx + 5, k0.cy + 107, 21.7);
        tq_toque_moveu(&n, 34, k0.cx + 5, k0.cy + 207, 21.8);
        CHECK(fabsf(tq_posicao_de(&n, TQ_TELA_BOTOES, TQ_C_NITRO).tamanho - 2) < 1e-4f, "100 para 200: 200%%");
        soltar(34, 21.9);
        soltar(32, 21.9);
        /* um terceiro dedo vai para o controle debaixo dele */
        tocar(35, TQ_C_FREIO, 22.0);
        tocar(36, TQ_C_FREIO, 22.0);
        tocar(37, TQ_C_PAUSA, 22.0);
        CHECK(n.selecionado == TQ_C_PAUSA, "o terceiro dedo seleciona o que tocou");
        tq_soltar_tudo(&n);
    }

    printf("18. cada tela tem as suas posições; voltar ao padrão é por tela\n");
    tq_restaurar_tela(&n, TQ_TELA_BOTOES);
    {
        const float gx = n.c[TQ_C_GAS].cx;
        tocar(40, TQ_C_GAS, 23.0);
        tq_toque_moveu(&n, 40, gx - 60, n.c[TQ_C_GAS].cy, 23.1);
        soltar(40, 23.2);
        CHECK(PERTO(n.c[TQ_C_GAS].cx, gx - 60), "movido em Botões");
        tq_ir_para_tela(&n, TQ_TELA_INCLINACAO);
        CHECK(n.modo == TQ_MODO_PILOTAGEM && n.aj.direcao == TQ_DIRECAO_INCLINACAO && n.c[TQ_C_RETO].visivel,
              "mostrando a tela da inclinação");
        CHECK(PERTO(n.c[TQ_C_GAS].cx, gx), "na inclinação, o Gás está no lugar padrão");
        tocar(41, TQ_C_GAS, 23.3);
        tq_toque_moveu(&n, 41, gx - 30, n.c[TQ_C_GAS].cy, 23.4);
        soltar(41, 23.5);
        tq_ir_para_tela(&n, TQ_TELA_MENU);
        CHECK(n.modo == TQ_MODO_MENU && (tq_eventos(&n) & TQ_EV_MODO), "mostrando o Menu");
        tq_ir_para_tela(&n, TQ_TELA_BOTOES);
        CHECK(PERTO(n.c[TQ_C_GAS].cx, gx - 60), "de volta a Botões, o Gás onde foi deixado");
        tq_restaurar_tela(&n, TQ_TELA_BOTOES);
        CHECK(PERTO(n.c[TQ_C_GAS].cx, gx), "padrão em Botões");
        tq_ir_para_tela(&n, TQ_TELA_INCLINACAO);
        CHECK(PERTO(n.c[TQ_C_GAS].cx, gx - 30), "a inclinação não voltou ao padrão");
        tq_ir_para_tela(&n, 99);
        CHECK(tq_tela_atual(&n) == TQ_TELA_INCLINACAO, "tela que não existe: nada");
    }

    printf("19. sair do modo de mover: os controles funcionam onde ficaram\n");
    tq_ir_para_tela(&n, TQ_TELA_BOTOES);
    {
        const float gx = n.c[TQ_C_GAS].cx, gy = n.c[TQ_C_GAS].cy;
        tocar(42, TQ_C_GAS, 24.0);
        tq_toque_moveu(&n, 42, gx - 300, gy - 100, 24.1);
        soltar(42, 24.2);
        tq_editar(&n, 0);
        CHECK(!n.editando && n.selecionado == -1, "fora do modo de mover");
        CHECK(PERTO(n.c[TQ_C_GAS].cx, gx - 300) && PERTO(n.c[TQ_C_GAS].cy, gy - 100), "o Gás ficou onde foi deixado");
        CHECK(tq_controle_em(&n, gx - 300, gy - 100) == TQ_C_GAS, "e é acertado lá");
        CHECK(tq_controle_em(&n, gx + 20, gy + 60) == -1, "o lugar antigo é do jogo");
        tocar(43, TQ_C_GAS, 24.5);
        tick(24.55);
        CHECK(s.rt == 1, "e acelera");
        soltar(43, 24.6);
        tick(24.8);
    }

    printf("20. a base do analógico se move, e a área do polegar vai junto\n");
    novo(analogico);
    pilotar();
    tq_editar(&n, 1);
    {
        const tq_info_controle b0 = n.c[TQ_C_ANALOGICO];
        CHECK(tq_controle_em(&n, b0.cx + 50, b0.cy) == TQ_C_ANALOGICO, "editando, a base é acertada como um círculo");
        CHECK(tq_controle_em(&n, b0.cx + 120, b0.cy - 80) == -1, "fora dela não");
        tq_toque_comecou(&n, 50, b0.cx, b0.cy, 25.0);
        tq_toque_moveu(&n, 50, b0.cx + 100, b0.cy - 50, 25.1);
        soltar(50, 25.2);
        CHECK(PERTO(n.c[TQ_C_ANALOGICO].cx, b0.cx + 100) && PERTO(n.base_x, b0.cx + 100), "a base de descanso andou");
        CHECK(PERTO(n.zona_x0, 100) && PERTO(n.zona_y0, H * 0.28f - 50), "a área andou junto (%.1f, %.1f)", n.zona_x0,
              n.zona_y0);
        tq_editar(&n, 0);
        CHECK(tq_controle_em(&n, 50, H - 30) == -1, "o canto que ficou fora da área é do jogo");
        CHECK(tq_controle_em(&n, W * 0.42f + 60, H - 60) == TQ_C_ANALOGICO, "e o novo lado é da área");
        CHECK(tq_controle_em(&n, W * 0.42f + 60, H - 30) == -1, "a área subiu 50: o pé dela é do jogo");
    }

    printf("21. posições guardadas: valores ruins, outro tamanho, outra tela\n");
    novo(NULL);
    pilotar();
    {
        const float gx = n.c[TQ_C_GAS].cx;
        tq_definir_posicao(&n, TQ_TELA_BOTOES, TQ_C_GAS, (tq_posicao){ -100, 0, 1 });
        CHECK(PERTO(n.c[TQ_C_GAS].cx, gx - 100), "posição carregada");
        tq_ajustes aj = n.aj;
        aj.escala = 0.8f;
        tq_definir_ajustes(&n, &aj);
        CHECK(PERTO(n.c[TQ_C_GAS].cx, W - 62 - 42 * 0.8f - 80), "no tamanho 80%%, o deslocamento também (cx=%.1f)",
              n.c[TQ_C_GAS].cx);
        aj.escala = 1;
        tq_definir_ajustes(&n, &aj);
        tq_definir_posicao(&n, TQ_TELA_BOTOES, TQ_C_GAS, (tq_posicao){ -5000, 0, 1 });
        CHECK(PERTO(n.c[TQ_C_GAS].cx, 42), "longe demais: mostrado na borda");
        CHECK(tq_posicao_de(&n, TQ_TELA_BOTOES, TQ_C_GAS).dx == -5000, "fora do modo de mover, o guardado não muda");
        tq_layout(&n, 1200, 600, 0, 0, 0, 0);
        CHECK(PERTO(n.c[TQ_C_GAS].cx, 42), "em outra tela, também dentro dela");
        tq_layout(&n, W, H, 62, 0, 62, 20);
        tq_definir_posicao(&n, TQ_TELA_BOTOES, TQ_C_GAS, (tq_posicao){ NAN, 3, NAN });
        tq_posicao p = tq_posicao_de(&n, TQ_TELA_BOTOES, TQ_C_GAS);
        CHECK(p.dx == 0 && p.dy == 0 && p.tamanho == 1, "valores ruins viram o padrão");
        tq_definir_posicao(&n, TQ_TELA_BOTOES, TQ_C_GAS, (tq_posicao){ 1, 2, 7 });
        CHECK(tq_posicao_de(&n, TQ_TELA_BOTOES, TQ_C_GAS).tamanho == 1, "tamanho fora de 50%% a 250%%: o padrão");
        tq_definir_posicao(&n, 9, TQ_C_GAS, (tq_posicao){ 1, 2, 1 });
        tq_definir_posicao(&n, TQ_TELA_BOTOES, -1, (tq_posicao){ 1, 2, 1 });
        tq_definir_posicao(&n, TQ_TELA_BOTOES, TQ_C_COUNT, (tq_posicao){ 1, 2, 1 });
        p = tq_posicao_de(&n, 9, TQ_C_GAS);
        CHECK(p.dx == 0 && p.dy == 0 && p.tamanho == 1, "tela que não existe: o padrão");
        p = tq_posicao_de(&n, TQ_TELA_BOTOES, 99);
        CHECK(p.dx == 0 && p.dy == 0 && p.tamanho == 1, "controle que não existe: o padrão");
        tq_restaurar_tela(&n, 7);
        tq_restaurar_tela(&n, -1);
        /* tamanho grande + borda: o controle inteiro dentro da tela */
        tq_definir_posicao(&n, TQ_TELA_BOTOES, TQ_C_GAS, (tq_posicao){ 400, 400, 2.5f });
        CHECK(PERTO(n.c[TQ_C_GAS].cx, W - n.c[TQ_C_GAS].meia_l) && PERTO(n.c[TQ_C_GAS].cy, H - n.c[TQ_C_GAS].meia_a),
              "grande e no canto: dentro da tela");
        tq_layout(&n, 300, 150, 0, 0, 0, 0);
        CHECK(n.s == 0.5f && n.c[TQ_C_GAS].meia_a * 2 > 150 && PERTO(n.c[TQ_C_GAS].cy, 75),
              "mais alto que a tela: no meio dela (s=%.2f, cy=%.1f)", n.s, n.c[TQ_C_GAS].cy);
    }

    printf("22. nomes guardados (ida e volta) e nomes para mostrar\n");
    {
        int ok_ids = 1, ok_nomes = 1;
        for (int c = 0; c < TQ_C_COUNT; c++) {
            if (tq_controle_por_id(tq_id_controle(c)) != c || strcmp(tq_id_controle(c), "?") == 0)
                ok_ids = 0;
            if (strcmp(tq_nome_controle(c), "?") == 0 || !tq_nome_controle(c)[0])
                ok_nomes = 0;
            for (int d = c + 1; d < TQ_C_COUNT; d++)
                if (strcmp(tq_id_controle(c), tq_id_controle(d)) == 0)
                    ok_ids = 0;
        }
        CHECK(ok_ids, "cada controle com um nome guardado só dele");
        CHECK(ok_nomes, "cada controle com um nome para mostrar");
        for (int tl = 0; tl < TQ_TELAS; tl++)
            CHECK(tq_tela_por_id(tq_id_tela(tl)) == tl && strcmp(tq_nome_tela(tl), "?") != 0, "tela %d", tl);
        CHECK(strcmp(tq_id_controle(TQ_C_PARCEIRO), "equipe") == 0 && strcmp(tq_id_tela(TQ_TELA_BOTOES), "botoes") == 0,
              "nomes estáveis");
        CHECK(tq_controle_por_id("nada") == -1 && tq_controle_por_id(NULL) == -1 && tq_tela_por_id("x") == -1 &&
                  tq_tela_por_id(NULL) == -1,
              "nome desconhecido");
        CHECK(strcmp(tq_id_controle(-1), "?") == 0 && strcmp(tq_nome_controle(TQ_C_COUNT), "?") == 0 &&
                  strcmp(tq_id_tela(TQ_TELAS), "?") == 0 && strcmp(tq_nome_tela(-1), "?") == 0,
              "número fora: ?");
        CHECK(strcmp(tq_nome_saida(TQ_O_RUP), "Anal\xC3\xB3gico direito \xE2\x86\x91") == 0 &&
                  strcmp(tq_nome_saida(99), "?") == 0 && strcmp(tq_nome_acao(TQ_ACAO_PARCEIRO), "?") != 0 &&
                  strcmp(tq_nome_acao(-1), "?") == 0,
              "nomes das saídas e das ações");
    }

    printf("23. limites e ajustes ruins\n");
    novo(NULL);
    pilotar();
    for (uintptr_t i = 1; i <= TQ_MAX_DEDOS + 3; i++)
        tq_toque_comecou(&n, 100 + i, n.c[TQ_C_GAS].cx, n.c[TQ_C_GAS].cy, 17.0);
    tick(17.1);
    CHECK(s.rt == 1, "muitos dedos sem quebrar");
    tq_soltar_tudo(&n);
    tick(17.3);
    CHECK(s.rt == 0 && s.botoes == 0, "soltar tudo");
    tq_toque_comecou(&n, 200, W / 2, H / 2, 17.4);
    tick(17.5);
    CHECK(s.botoes == 0 && s.rt == 0, "toque fora dos controles não aperta nada");
    tq_toque_comecou(&n, 0, n.c[TQ_C_GAS].cx, n.c[TQ_C_GAS].cy, 17.5);
    tick(17.55);
    CHECK(s.rt == 0, "id 0 não vale");
    tq_toque_comecou(&n, 201, n.c[TQ_C_GAS].cx, n.c[TQ_C_GAS].cy, 17.6);
    tq_toque_comecou(&n, 201, n.c[TQ_C_FREIO].cx, n.c[TQ_C_FREIO].cy, 17.6);
    tick(17.65);
    CHECK(s.rt == 1 && s.lt == 0, "o mesmo dedo duas vezes: só o primeiro");
    soltar(201, 17.7);
    tq_toque_moveu(&n, 999, 10, 10, 17.6);
    soltar(999, 17.7);
    novo(ajustes_ruins);
    CHECK(n.aj.escala == 1.0f && n.aj.direcao == TQ_DIRECAO_BOTOES && n.aj.mapa[0] == TQ_O_A, "ajustes corrigidos");
    {
        tq_nucleo m;
        tq_ajustes aj;
        tq_ajustes_padrao(&aj);
        tq_iniciar(&m, &aj);
        tq_estado_controle e;
        tq_tick(&m, 1, &e);
        CHECK(tq_controle_em(&m, 10, 10) == -1 && e.botoes == 0, "sem tela ainda: nada");
        tq_toque_comecou(&m, 1, 10, 10, 1);
        tq_editar(&m, 1);
        tq_toque_comecou(&m, 2, 10, 10, 1);
        tq_toque_moveu(&m, 2, 20, 20, 1);
        CHECK(m.dedos[0].controle == -1, "e nada guardado");
    }

    printf("\n%d checagens, %d falha(s)\n", checagens, falhas);
    return falhas ? 1 : 0;
}
