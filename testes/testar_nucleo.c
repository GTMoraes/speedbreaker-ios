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

/* iPhone 17 deitado: 874 x 402 pontos, margens seguras 59 / 0 / 59 / 21 */
#define W 874.0f
#define H 402.0f

static tq_nucleo n;
static tq_estado_controle s;

static void novo(void (*mudar)(tq_ajustes *))
{
    tq_ajustes aj;
    tq_ajustes_padrao(&aj);
    if (mudar)
        mudar(&aj);
    tq_iniciar(&n, &aj);
    tq_layout(&n, W, H, 59, 0, 59, 21);
    tq_eventos(&n);
}

static void tick(double t) { tq_tick(&n, t, &s); }
static int botao(int o) { return (s.botoes >> o) & 1u; }
static void tocar(uintptr_t id, int c, double t) { tq_toque_comecou(&n, id, n.c[c].cx, n.c[c].cy, t); }
static void mover_para(uintptr_t id, int c, double t) { tq_toque_moveu(&n, id, n.c[c].cx, n.c[c].cy, t); }

static void sem_sobreposicao(const char *nome)
{
    for (int i = 0; i < TQ_C_COUNT; i++) {
        const tq_info_controle *a = &n.c[i];
        if (!a->visivel || i == TQ_C_ANALOGICO)
            continue;
        const float ra = a->forma == TQ_FORMA_CIRCULO ? a->r : a->meia_l;
        CHECK(a->cx - ra >= 0 && a->cx + ra <= W && a->cy - a->r >= 0 && a->cy + a->r <= H,
              "%s: controle %d sai da tela (%.0f,%.0f r=%.0f)", nome, i, a->cx, a->cy, a->r);
        for (int j = i + 1; j < TQ_C_COUNT; j++) {
            const tq_info_controle *b = &n.c[j];
            if (!b->visivel || j == TQ_C_ANALOGICO)
                continue;
            if (a->forma == TQ_FORMA_CIRCULO && b->forma == TQ_FORMA_CIRCULO) {
                const float d = hypotf(a->cx - b->cx, a->cy - b->cy);
                CHECK(d >= a->r + b->r, "%s: controles %d e %d se sobrepõem (dist %.1f < %.1f)", nome, i, j, d,
                      a->r + b->r);
            } else {
                const float ax = a->forma == TQ_FORMA_CIRCULO ? a->r : a->meia_l;
                const float bx = b->forma == TQ_FORMA_CIRCULO ? b->r : b->meia_l;
                const int separados = fabsf(a->cx - b->cx) >= ax + bx || fabsf(a->cy - b->cy) >= a->r + b->r;
                CHECK(separados, "%s: controles %d e %d se sobrepõem (retângulo)", nome, i, j);
            }
        }
    }
}

static void escala_08(tq_ajustes *a) { a->escala = 0.8f; }
static void escala_15(tq_ajustes *a) { a->escala = 1.5f; }
static void analogico(tq_ajustes *a) { a->direcao = TQ_DIRECAO_ANALOGICO; }
static void inclinacao(tq_ajustes *a) { a->direcao = TQ_DIRECAO_INCLINACAO; }
static void manual(tq_ajustes *a) { a->cambio_manual = 1; }
static void tudo_15(tq_ajustes *a) { a->escala = 1.5f; a->cambio_manual = 1; a->direcao = TQ_DIRECAO_INCLINACAO; }
static void tudo_15b(tq_ajustes *a) { a->escala = 1.5f; a->cambio_manual = 1; }
static void sem_pausa_menu(tq_ajustes *a) { a->pausa_vai_para_menu = 0; }
static void freio_mao_no_x(tq_ajustes *a) { a->mapa[TQ_ACAO_FREIO_MAO] = TQ_O_X; }
static void ajustes_ruins(tq_ajustes *a) { a->escala = 9; a->direcao = 7; a->mapa[0] = 200; }

int main(void)
{
    printf("1. posições: nada sai da tela nem se sobrepõe\n");
    void (*variantes[])(tq_ajustes *) = { NULL, escala_08, escala_15, analogico, inclinacao, manual, tudo_15, tudo_15b };
    for (unsigned v = 0; v < sizeof variantes / sizeof *variantes; v++) {
        for (int modo = 0; modo < 2; modo++)
            for (int mais = 0; mais < 2; mais++) {
                novo(variantes[v]);
                tq_definir_modo(&n, modo);
                if (mais && modo == TQ_MODO_PILOTAGEM) {
                    tocar(1, TQ_C_MAIS, 0);
                    tq_toque_acabou(&n, 1, 0.05);
                    CHECK(n.c[TQ_C_RESET].visivel, "o + abre a segunda fileira");
                }
                char nome[64];
                snprintf(nome, sizeof nome, "variante %u modo %d mais %d", v, modo, mais);
                sem_sobreposicao(nome);
            }
    }

    printf("2. menu: só os controles do menu; o meio da tela fica para o jogo\n");
    novo(NULL);
    CHECK(n.modo == TQ_MODO_MENU, "começa no modo Menu");
    CHECK(n.c[TQ_C_DPAD].visivel && n.c[TQ_C_OK].visivel && !n.c[TQ_C_GAS].visivel, "visíveis do menu");
    CHECK(tq_controle_em(&n, W / 2, H / 2) == -1, "o meio da tela não é nosso");
    CHECK(strcmp(n.c[TQ_C_MODO].rotulo, "PILOTAR") == 0, "rótulo do modo no menu");

    printf("3. toque rápido segura o botão por 100 ms\n");
    tocar(1, TQ_C_OK, 1.00);
    tq_toque_acabou(&n, 1, 1.01);
    tick(1.05);
    CHECK(botao(TQ_O_A), "A apertado logo depois do toque");
    tick(1.11);
    CHECK(!botao(TQ_O_A), "A solto depois de 100 ms");

    printf("4. direcional: direção pelo ângulo, desliza entre direções\n");
    novo(NULL);
    {
        const tq_info_controle *d = &n.c[TQ_C_DPAD];
        tq_toque_comecou(&n, 2, d->cx, d->cy - d->r * 0.7f, 2.0);
        tick(2.01);
        CHECK(botao(TQ_O_DUP) && !botao(TQ_O_DRIGHT), "cima");
        tq_toque_moveu(&n, 2, d->cx + d->r * 0.7f, d->cy, 2.2);
        tick(2.21);
        CHECK(botao(TQ_O_DRIGHT), "direita");
        tick(2.3);
        CHECK(!botao(TQ_O_DUP), "cima solto depois do aperto mínimo");
        tq_toque_moveu(&n, 2, d->cx + 1, d->cy, 2.4);
        tick(2.6);
        CHECK(!botao(TQ_O_DRIGHT) && !botao(TQ_O_DUP) && !botao(TQ_O_DDOWN) && !botao(TQ_O_DLEFT), "centro: nada");
        tq_toque_acabou(&n, 2, 2.7);
    }

    printf("5. pilotagem: gás, freio e nitro deslizando o dedo\n");
    novo(NULL);
    tq_definir_modo(&n, TQ_MODO_PILOTAGEM);
    CHECK(tq_eventos(&n) & TQ_EV_MODO, "evento de modo");
    CHECK(!n.c[TQ_C_DPAD].visivel && n.c[TQ_C_GAS].visivel, "visíveis da pilotagem");
    tocar(3, TQ_C_GAS, 3.0);
    tick(3.05);
    CHECK(s.rt == 1 && s.lt == 0, "gás = RT");
    mover_para(3, TQ_C_NITRO, 3.3);
    tick(3.35);
    CHECK(s.rt == 1 && botao(TQ_O_B), "nitro = B e mantém o gás");
    tick(3.5);
    CHECK(s.rt == 1 && botao(TQ_O_B), "nitro segurado (depois do aperto mínimo) continua com gás");
    mover_para(3, TQ_C_FREIO, 3.6);
    tick(3.8);
    CHECK(s.lt == 1 && s.rt == 0 && !botao(TQ_O_B), "freio = LT, sem gás nem nitro");
    tq_toque_moveu(&n, 3, W / 2, H / 2, 3.9);
    tick(4.0);
    CHECK(s.lt == 1, "fora dos pedais, fica no último");
    tq_toque_acabou(&n, 3, 4.1);
    tick(4.3);
    CHECK(s.lt == 0 && s.rt == 0, "soltou: nada");

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
    for (int i = 0; i < 20; i++)
        tick(t += 1.0 / 60);
    CHECK(s.lx == -1.0f, "fim de curso à esquerda (lx=%.2f)", s.lx);
    tq_toque_acabou(&n, 4, t);
    for (int i = 0; i < 6; i++)
        tick(t += 1.0 / 60);
    CHECK(s.lx == 0.0f, "soltou: volta ao centro em ~0,08 s (lx=%.2f)", s.lx);

    printf("7. analógico\n");
    novo(analogico);
    tq_definir_modo(&n, TQ_MODO_PILOTAGEM);
    {
        const float x = 59 + 30, y = H - 80;
        CHECK(tq_controle_em(&n, x, y) == TQ_C_ANALOGICO, "a área do polegar é do analógico");
        tq_toque_comecou(&n, 5, x, y, 6.0);
        tq_toque_moveu(&n, 5, x + 12 * n.u, y, 6.1);
        tick(6.1);
        CHECK(fabsf(s.lx - 1) < 1e-4f, "12 unidades à direita = fim de curso (lx=%.3f)", s.lx);
        tq_toque_moveu(&n, 5, x - 6 * n.u, y, 6.2);
        tick(6.2);
        CHECK(fabsf(s.lx + 0.5f) < 1e-3f, "6 unidades à esquerda = metade (lx=%.3f)", s.lx);
        tq_toque_acabou(&n, 5, 6.3);
        tick(6.3);
        CHECK(s.lx == 0, "soltou: centro");
        CHECK(tq_controle_em(&n, n.c[TQ_C_GAS].cx, n.c[TQ_C_GAS].cy) == TQ_C_GAS, "o gás continua sendo o gás");
    }

    printf("8. inclinação\n");
    CHECK(fabsf(tq_inclinacao_relativa(170, -170) + 20) < 1e-4f, "passa de 180 para -180");
    CHECK(fabsf(tq_inclinacao_relativa(-170, 170) - 20) < 1e-4f, "passa de -180 para 180");
    novo(inclinacao);
    tq_definir_modo(&n, TQ_MODO_PILOTAGEM);
    CHECK(n.c[TQ_C_RETO].visivel && n.c[TQ_C_FREIO].cx < W / 2, "Reto aparece e o freio vai para a esquerda");
    tq_definir_inclinacao(&n, 15);
    tick(7.0);
    CHECK(fabsf(s.lx - 0.5f) < 1e-4f, "15 graus = metade (lx=%.3f)", s.lx);
    tq_definir_inclinacao(&n, 1);
    tick(7.1);
    CHECK(s.lx == 0, "1 grau = reto");
    tq_definir_inclinacao(&n, -45);
    tick(7.2);
    CHECK(s.lx == -1, "45 graus = fim de curso");
    tocar(6, TQ_C_RETO, 7.3);
    CHECK(tq_eventos(&n) & TQ_EV_RETO, "o Reto pede recalibrar");
    tq_toque_acabou(&n, 6, 7.35);

    printf("9. câmera: toque troca (RB), segurar olha para trás (RS)\n");
    novo(NULL);
    tq_definir_modo(&n, TQ_MODO_PILOTAGEM);
    tocar(7, TQ_C_CAMERA, 8.0);
    tick(8.05);
    CHECK(!botao(TQ_O_RB) && !botao(TQ_O_RS), "nada enquanto não decide");
    tq_toque_acabou(&n, 7, 8.10);
    tick(8.12);
    CHECK(botao(TQ_O_RB), "toque rápido: RB");
    tick(8.25);
    CHECK(!botao(TQ_O_RB), "RB solto");
    tocar(7, TQ_C_CAMERA, 9.0);
    tick(9.2);
    CHECK(!botao(TQ_O_RS), "antes de 0,25 s: nada");
    tick(9.3);
    CHECK(botao(TQ_O_RS) && !botao(TQ_O_RB), "segurando: RS");
    tq_toque_acabou(&n, 7, 9.5);
    tick(9.51);
    CHECK(!botao(TQ_O_RS) && !botao(TQ_O_RB), "soltou depois de segurar: nem RS nem RB");

    printf("10. pausa: Start, e muda para o Menu\n");
    tocar(8, TQ_C_PAUSA, 10.0);
    tick(10.05);
    CHECK(botao(TQ_O_START), "Start apertado");
    tq_toque_acabou(&n, 8, 10.1);
    CHECK(n.modo == TQ_MODO_MENU, "foi para o Menu");
    CHECK(tq_eventos(&n) & TQ_EV_MODO, "evento de modo");
    novo(sem_pausa_menu);
    tq_definir_modo(&n, TQ_MODO_PILOTAGEM);
    tocar(8, TQ_C_PAUSA, 11.0);
    tq_toque_acabou(&n, 8, 11.1);
    CHECK(n.modo == TQ_MODO_PILOTAGEM, "com o ajuste desligado, fica na pilotagem");

    printf("11. botão de modo e trocar de modo com o pé no gás\n");
    novo(NULL);
    tocar(9, TQ_C_MODO, 12.0);
    CHECK(n.modo == TQ_MODO_PILOTAGEM && strcmp(n.c[TQ_C_MODO].rotulo, "MENU") == 0, "Pilotar troca o modo");
    tq_toque_acabou(&n, 9, 12.05);
    tocar(10, TQ_C_GAS, 12.1);
    tocar(9, TQ_C_MODO, 12.2);
    tick(12.4);
    CHECK(n.modo == TQ_MODO_MENU && s.rt == 0, "no menu, o gás que ficou apertado não vale");
    tq_toque_acabou(&n, 9, 12.5);
    tq_toque_acabou(&n, 10, 12.5);

    printf("12. + abre reset, mapa e SMS; usar um fecha a fileira\n");
    novo(NULL);
    tq_definir_modo(&n, TQ_MODO_PILOTAGEM);
    tocar(11, TQ_C_MAIS, 13.0);
    tq_toque_acabou(&n, 11, 13.05);
    CHECK(n.c[TQ_C_RESET].visivel && n.c[TQ_C_MAPA].visivel && n.c[TQ_C_SMS].visivel, "fileira aberta");
    tocar(12, TQ_C_RESET, 13.2);
    tick(13.25);
    CHECK(botao(TQ_O_LB), "reset = LB");
    tq_toque_acabou(&n, 12, 13.3);
    CHECK(!n.c[TQ_C_RESET].visivel, "fileira fechada");

    printf("13. ajustes, Back + Start, reatribuir\n");
    tocar(13, TQ_C_AJUSTES, 14.0);
    CHECK(!(tq_eventos(&n) & TQ_EV_AJUSTES), "não abre ao encostar");
    tq_toque_acabou(&n, 13, 14.05);
    CHECK(tq_eventos(&n) & TQ_EV_AJUSTES, "abre ao soltar");
    tq_pulso(&n, (1u << TQ_O_BACK) | (1u << TQ_O_START), 14.1, 0.25);
    tick(14.2);
    CHECK(botao(TQ_O_BACK) && botao(TQ_O_START), "Back + Start juntos");
    tick(14.4);
    CHECK(!botao(TQ_O_BACK) && !botao(TQ_O_START), "e soltos depois");
    novo(freio_mao_no_x);
    tq_definir_modo(&n, TQ_MODO_PILOTAGEM);
    tocar(14, TQ_C_FREIO_MAO, 15.0);
    tick(15.05);
    CHECK(botao(TQ_O_X) && !botao(TQ_O_A), "freio de mão reatribuído para X");
    tq_toque_acabou(&n, 14, 15.1);

    printf("14. marchas no analógico direito\n");
    novo(manual);
    tq_definir_modo(&n, TQ_MODO_PILOTAGEM);
    tocar(15, TQ_C_SOBE, 16.0);
    tick(16.05);
    CHECK(s.ry == -1, "Marcha + = analógico direito para cima (ry=%.0f)", s.ry);
    tq_toque_acabou(&n, 15, 16.06);
    tick(16.2);
    tocar(16, TQ_C_DESCE, 16.3);
    tick(16.35);
    CHECK(s.ry == 1, "Marcha - = para baixo (ry=%.0f)", s.ry);
    tq_toque_acabou(&n, 16, 16.4);

    printf("15. limites e ajustes ruins\n");
    novo(NULL);
    tq_definir_modo(&n, TQ_MODO_PILOTAGEM);
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
    tq_toque_moveu(&n, 999, 10, 10, 17.6);
    tq_toque_acabou(&n, 999, 17.7);
    novo(ajustes_ruins);
    CHECK(n.aj.escala == 1.0f && n.aj.direcao == TQ_DIRECAO_BOTOES && n.aj.mapa[0] == TQ_O_A, "ajustes corrigidos");
    CHECK(strcmp(tq_nome_saida(TQ_O_RUP), "Anal\xC3\xB3gico direito \xE2\x86\x91") == 0 && strcmp(tq_nome_saida(99), "?") == 0,
          "nomes");

    printf("\n%d checagens, %d falha(s)\n", checagens, falhas);
    return falhas ? 1 : 0;
}
