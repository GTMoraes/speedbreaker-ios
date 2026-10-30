/*
 * Testes da ponte com o SDL (Toque/ponte_sdl.c), com um SDL e um runtime de mentira definidos aqui.
 * A ponte acha as funções por dlsym, como no iPhone: o executável do teste precisa exportá-las (-rdynamic).
 *   cc -std=c11 -Wall -Wextra -I Toque -I Toque/SDL/include Toque/ponte_sdl.c testes/testar_ponte.c \
 *      -rdynamic -ldl -lm -o /tmp/tp && /tmp/tp
 * Com -DSEM_FUNCOES, duas funções somem (como num executável de outra versão) e o teste confere que a camada
 * fica desligada sem quebrar nada.
 */
#include "ponte_sdl.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_stdinc.h>

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

/* ---------- o SDL de mentira ---------- */

static SDL_InitFlags f_ligado;          /* o que SDL_WasInit responde */
static SDL_InitFlags f_pedido_wasinit;
static int f_attach, f_detach, f_open, f_close, f_free;
static int f_attach_falha, f_open_falha, f_eixo_falha;
static SDL_VirtualJoystickDesc f_desc;
static SDL_JoystickID f_proximo_id = 41, f_id_ligado, f_id_desligado;
static struct SDL_Joystick { int x; } f_joystick;
static SDL_Joystick *f_js_fechado;
static int f_n_eixo, f_n_botao;          /* chamadas */
static int f_eixo[SDL_GAMEPAD_AXIS_COUNT];
static int f_botao[SDL_GAMEPAD_BUTTON_COUNT];
static int f_ultimo_eixo = -1, f_ultimo_botao = -1;
static int f_aberto_pelo_jogo;
#ifndef SEM_FUNCOES
static SDL_JoystickID f_outros[4];
static int f_n_outros;
static int f_lista_nula;
#endif
static int f_settings, f_modal;

SDL_InitFlags SDL_WasInit(SDL_InitFlags flags)
{
    f_pedido_wasinit = flags;
    return f_ligado & flags;
}

SDL_JoystickID SDL_AttachVirtualJoystick(const SDL_VirtualJoystickDesc *desc)
{
    f_attach++;
    f_desc = *desc;
    if (f_attach_falha)
        return 0;
    f_id_ligado = f_proximo_id++;
    return f_id_ligado;
}

bool SDL_DetachVirtualJoystick(SDL_JoystickID id)
{
    f_detach++;
    f_id_desligado = id;
    return true;
}

SDL_Joystick *SDL_OpenJoystick(SDL_JoystickID id)
{
    f_open++;
    if (f_open_falha || id != f_id_ligado)
        return NULL;
    return &f_joystick;
}

void SDL_CloseJoystick(SDL_Joystick *js)
{
    f_close++;
    f_js_fechado = js;
}

bool SDL_SetJoystickVirtualAxis(SDL_Joystick *js, int axis, Sint16 value)
{
    f_n_eixo++;
    if (js != &f_joystick || axis < 0 || axis >= SDL_GAMEPAD_AXIS_COUNT)
        return false;
    if (f_eixo_falha) {
        f_eixo_falha--;
        return false;
    }
    f_eixo[axis] = value;
    f_ultimo_eixo = axis;
    return true;
}

bool SDL_SetJoystickVirtualButton(SDL_Joystick *js, int button, bool down)
{
    f_n_botao++;
    if (js != &f_joystick || button < 0 || button >= SDL_GAMEPAD_BUTTON_COUNT)
        return false;
    f_botao[button] = down;
    f_ultimo_botao = button;
    return true;
}

SDL_Gamepad *SDL_GetGamepadFromID(SDL_JoystickID id)
{
    static int qualquer;
    return f_aberto_pelo_jogo && id == f_id_ligado ? (SDL_Gamepad *)(void *)&qualquer : NULL;
}

#ifndef SEM_FUNCOES
SDL_JoystickID *SDL_GetGamepads(int *count)
{
    static SDL_JoystickID lista[8];
    int n = 0;
    if (f_lista_nula) {
        *count = 0;
        return NULL;
    }
    if (f_id_ligado && f_attach > f_detach)
        lista[n++] = f_id_ligado;
    for (int i = 0; i < f_n_outros; i++)
        lista[n++] = f_outros[i];
    *count = n;
    return lista;
}
#endif

void SDL_free(void *mem)
{
    if (mem)
        f_free++;
}

const char *SDL_GetError(void) { return "erro de mentira"; }

/* ui::SettingsOpen() e ui::HasModal() do runtime */
bool _ZN2ui12SettingsOpenEv(void);
bool _ZN2ui12SettingsOpenEv(void) { return f_settings; }
#ifndef SEM_FUNCOES
bool _ZN2ui8HasModalEv(void);
bool _ZN2ui8HasModalEv(void) { return f_modal; }
#endif

/* ---------- ajudantes ---------- */

#ifndef SEM_FUNCOES
static void zerar_contas(void) { f_n_eixo = f_n_botao = 0; }
#endif

static tq_estado_controle estado(uint32_t botoes, float lx, float ly, float rx, float ry, float lt, float rt)
{
    tq_estado_controle s;
    memset(&s, 0, sizeof s);
    s.botoes = botoes;
    s.lx = lx;
    s.ly = ly;
    s.rx = rx;
    s.ry = ry;
    s.lt = lt;
    s.rt = rt;
    return s;
}

static void enviar(tq_estado_controle s) { ps_enviar(&s); }

#ifdef SEM_FUNCOES
int main(void)
{
    printf("Sem duas funções no executável: a camada fica desligada\n");
    f_ligado = SDL_INIT_GAMEPAD;
    f_modal = 1;
    f_settings = 1;
    CHECK(ps_carregar() == 0, "carregar devolve 0");
    CHECK(ps_funcoes_achadas() == ps_funcoes_total() - 2, "achou %d de %d", ps_funcoes_achadas(), ps_funcoes_total());
    CHECK(strcmp(ps_faltando(), "SDL_GetGamepads _ZN2ui8HasModalEv") == 0, "faltando: [%s]", ps_faltando());
    CHECK(ps_sdl_pronto() == 0, "não fica pronto");
    CHECK(ps_conectar() == 0 && f_attach == 0, "não conecta");
    enviar(estado(1, 1, 0, 0, 0, 1, 1));
    CHECK(f_n_eixo == 0 && f_n_botao == 0, "não manda nada");
    CHECK(ps_outros_controles() == 0, "outros: 0");
    CHECK(ps_tela_cheia() == 0 && ps_ajustes_abertos() == 0, "não chama as funções que faltam");
    CHECK(ps_aberto_pelo_jogo() == 0, "não aberto");
    char buf[600];
    ps_status_texto(buf, sizeof buf);
    CHECK(strstr(buf, "Faltando: SDL_GetGamepads _ZN2ui8HasModalEv") != NULL, "status: [%s]", buf);
    CHECK(ps_carregar() == 0 && ps_funcoes_achadas() == ps_funcoes_total() - 2, "carregar de novo não muda nada");
    printf("\n%d checagens, %d falha(s)\n", checagens, falhas);
    return falhas ? 1 : 0;
}
#else
int main(void)
{
    char buf[600];

    printf("1. achar as funções\n");
    CHECK(ps_funcoes_achadas() == 0, "nada antes de carregar");
    CHECK(ps_carregar() == 1, "achou todas");
    CHECK(ps_funcoes_achadas() == 13 && ps_funcoes_total() == 13, "13 de 13 (achou %d)", ps_funcoes_achadas());
    CHECK(ps_faltando()[0] == 0, "nada faltando: [%s]", ps_faltando());
    CHECK(ps_carregar() == 1, "carregar de novo");

    printf("2. antes de o runtime ligar o SDL dos controles\n");
    f_ligado = SDL_INIT_VIDEO;
    CHECK(ps_sdl_pronto() == 0, "não pronto");
    CHECK(f_pedido_wasinit == SDL_INIT_GAMEPAD, "pergunta pelo SDL_INIT_GAMEPAD (0x%x)", (unsigned)f_pedido_wasinit);
    CHECK(ps_conectar() == 0 && f_attach == 0, "não tenta conectar");
    CHECK(ps_outros_controles() == 0, "outros: 0");
    enviar(estado(1, 1, 0, 0, 0, 0, 0));
    CHECK(f_n_eixo == 0 && f_n_botao == 0, "sem controle, não manda nada");

    printf("3. conectar\n");
    f_ligado = SDL_INIT_VIDEO | SDL_INIT_GAMEPAD;
    CHECK(ps_sdl_pronto() == 1, "pronto");
    zerar_contas();
    CHECK(ps_conectar() == 1, "conectou");
    CHECK(f_attach == 1 && f_open == 1, "attach e open uma vez cada");
    CHECK(f_desc.version == sizeof(SDL_VirtualJoystickDesc), "desc.version (%u)", (unsigned)f_desc.version);
    CHECK(f_desc.type == SDL_JOYSTICK_TYPE_GAMEPAD, "tipo gamepad");
    CHECK(f_desc.naxes == SDL_GAMEPAD_AXIS_COUNT && f_desc.nbuttons == SDL_GAMEPAD_BUTTON_COUNT,
          "todos os eixos e botões (%u, %u)", (unsigned)f_desc.naxes, (unsigned)f_desc.nbuttons);
    CHECK(f_desc.nhats == 0 && f_desc.button_mask == 0 && f_desc.axis_mask == 0 && f_desc.ntouchpads == 0 &&
              f_desc.nsensors == 0,
          "sem hat, sem máscaras (o SDL preenche na ordem do gamepad), sem touchpad e sensor");
    CHECK(f_desc.vendor_id == 0 && f_desc.product_id == 0, "sem vendor/product");
    CHECK(f_desc.name && strcmp(f_desc.name, "Toque (NFS Carbon)") == 0, "nome");
    CHECK(f_desc.Rumble != NULL && f_desc.RumbleTriggers == NULL && f_desc.SetLED == NULL, "só rumble");
    CHECK(ps_conectado() == 1 && ps_id() == 41, "conectado, id 41");

    printf("4. estado neutro logo ao conectar\n");
    CHECK(f_n_eixo == 6 && f_n_botao == 14, "6 eixos e 14 botões mandados (%d, %d)", f_n_eixo, f_n_botao);
    CHECK(f_eixo[SDL_GAMEPAD_AXIS_LEFT_TRIGGER] == -32768 && f_eixo[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] == -32768,
          "gatilhos soltos = -32768 (%d, %d)", f_eixo[SDL_GAMEPAD_AXIS_LEFT_TRIGGER],
          f_eixo[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER]);
    CHECK(f_eixo[0] == 0 && f_eixo[1] == 0 && f_eixo[2] == 0 && f_eixo[3] == 0, "analógicos no centro");
    int algum = 0;
    for (int i = 0; i < SDL_GAMEPAD_BUTTON_COUNT; i++)
        algum |= f_botao[i];
    CHECK(!algum, "nenhum botão");
    CHECK(ps_conectar() == 1 && f_attach == 1, "conectar de novo não cria outro");

    printf("5. só o que mudou\n");
    zerar_contas();
    enviar(estado(0, 0, 0, 0, 0, 0, 0));
    CHECK(f_n_eixo == 0 && f_n_botao == 0, "mesmo estado: nada (%d, %d)", f_n_eixo, f_n_botao);
    enviar(estado(1u << TQ_O_A, 0, 0, 0, 0, 0, 0));
    CHECK(f_n_eixo == 0 && f_n_botao == 1 && f_ultimo_botao == SDL_GAMEPAD_BUTTON_SOUTH && f_botao[SDL_GAMEPAD_BUTTON_SOUTH],
          "A: só o SOUTH");
    zerar_contas();
    enviar(estado(1u << TQ_O_A, 0, 0, 0, 0, 0, 1));
    CHECK(f_n_eixo == 1 && f_n_botao == 0 && f_ultimo_eixo == SDL_GAMEPAD_AXIS_RIGHT_TRIGGER &&
              f_eixo[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] == 32767,
          "RT: só o gatilho direito, 32767");
    zerar_contas();
    enviar(estado(0, 0, 0, 0, 0, 0, 1));
    CHECK(f_n_botao == 1 && !f_botao[SDL_GAMEPAD_BUTTON_SOUTH] && f_n_eixo == 0, "soltar A");

    printf("6. cada botão no botão certo do SDL\n");
    const int esperado[TQ_O_BOTOES] = {
        SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH,
        SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_BACK,
        SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK,
        SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT,
        SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
    };
    const char *nomes[TQ_O_BOTOES] = { "A", "B", "X", "Y", "LB", "RB", "Back", "Start", "LS", "RS",
                                       "cima", "baixo", "esquerda", "direita" };
    for (int o = 0; o < TQ_O_BOTOES; o++) {
        zerar_contas();
        enviar(estado(1u << o, 0, 0, 0, 0, 0, 0));
        int outros = 0;
        for (int b = 0; b < SDL_GAMEPAD_BUTTON_COUNT; b++)
            if (b != esperado[o])
                outros |= f_botao[b];
        CHECK(f_botao[esperado[o]] && !outros, "%s -> botão %d do SDL, e só ele", nomes[o], esperado[o]);
        enviar(estado(0, 0, 0, 0, 0, 0, 0));
        CHECK(!f_botao[esperado[o]], "%s solto", nomes[o]);
    }
    zerar_contas();
    enviar(estado(0xFFFFC000u, 0, 0, 0, 0, 0, 0)); /* bits acima dos botões (LT, RT, analógico direito) */
    CHECK(f_n_botao == 0, "bits que não são botões não viram botão");

    printf("7. eixos\n");
    enviar(estado(0, 1, -1, 0.5f, -0.5f, 0, 0));
    CHECK(f_eixo[SDL_GAMEPAD_AXIS_LEFTX] == 32767 && f_eixo[SDL_GAMEPAD_AXIS_LEFTY] == -32767,
          "esquerdo: 32767 e -32767 (%d, %d)", f_eixo[SDL_GAMEPAD_AXIS_LEFTX], f_eixo[SDL_GAMEPAD_AXIS_LEFTY]);
    CHECK(f_eixo[SDL_GAMEPAD_AXIS_RIGHTX] == 16384 && f_eixo[SDL_GAMEPAD_AXIS_RIGHTY] == -16384,
          "direito: metade (%d, %d)", f_eixo[SDL_GAMEPAD_AXIS_RIGHTX], f_eixo[SDL_GAMEPAD_AXIS_RIGHTY]);
    enviar(estado(0, 3, -7, 0, 0, 2, -1));
    CHECK(f_eixo[SDL_GAMEPAD_AXIS_LEFTX] == 32767 && f_eixo[SDL_GAMEPAD_AXIS_LEFTY] == -32767, "fora da faixa: limita");
    CHECK(f_eixo[SDL_GAMEPAD_AXIS_LEFT_TRIGGER] == 32767 && f_eixo[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] == -32768,
          "gatilhos fora da faixa: limita");
    enviar(estado(0, NAN, 0, 0, 0, NAN, 0));
    CHECK(f_eixo[SDL_GAMEPAD_AXIS_LEFTX] == 0 && f_eixo[SDL_GAMEPAD_AXIS_LEFT_TRIGGER] == -32768, "NaN vira neutro");
    enviar(estado(0, 0, 0, 0, 0, 0.5f, 0));
    CHECK(f_eixo[SDL_GAMEPAD_AXIS_LEFT_TRIGGER] >= -1 && f_eixo[SDL_GAMEPAD_AXIS_LEFT_TRIGGER] <= 0,
          "gatilho pela metade: perto de 0 (%d)", f_eixo[SDL_GAMEPAD_AXIS_LEFT_TRIGGER]);
    enviar(estado(0, 0, 0, 0, 0, 0, 0));

    printf("8. o SDL recusou: tenta de novo no próximo envio\n");
    f_eixo_falha = 1;
    zerar_contas();
    enviar(estado(0, 0.25f, 0, 0, 0, 0, 0));
    CHECK(f_eixo[SDL_GAMEPAD_AXIS_LEFTX] == 0 && f_n_eixo == 1, "recusou");
    enviar(estado(0, 0.25f, 0, 0, 0, 0, 0));
    CHECK(f_eixo[SDL_GAMEPAD_AXIS_LEFTX] == 8192 && f_n_eixo == 2, "mandou de novo e foi (%d)",
          f_eixo[SDL_GAMEPAD_AXIS_LEFTX]);
    enviar(estado(0, 0, 0, 0, 0, 0, 0));

    printf("9. rumble que o jogo pede\n");
    uint16_t grave, agudo;
    uint32_t pedidos;
    ps_rumble(&grave, &agudo, &pedidos);
    CHECK(grave == 0 && agudo == 0 && pedidos == 0, "nada ainda");
    CHECK(f_desc.Rumble(f_desc.userdata, 0x1234, 0xABCD), "o callback aceita");
    ps_rumble(&grave, &agudo, &pedidos);
    CHECK(grave == 0x1234 && agudo == 0xABCD && pedidos == 1, "guardou (%04x %04x %u)", grave, agudo, pedidos);
    f_desc.Rumble(f_desc.userdata, 0xFFFF, 0);
    ps_rumble(&grave, &agudo, &pedidos);
    CHECK(grave == 0xFFFF && agudo == 0 && pedidos == 2, "o último pedido vale");

    printf("10. controles de verdade e quem abriu o virtual\n");
    CHECK(ps_outros_controles() == 0 && f_free == 1, "só o virtual na lista (e a lista foi liberada)");
    f_outros[0] = 3;
    f_n_outros = 1;
    CHECK(ps_outros_controles() == 1, "um controle de verdade");
    f_lista_nula = 1;
    CHECK(ps_outros_controles() == 0, "lista vazia do SDL");
    f_lista_nula = 0;
    CHECK(ps_aberto_pelo_jogo() == 0, "o jogo ainda não abriu");
    f_aberto_pelo_jogo = 1;
    CHECK(ps_aberto_pelo_jogo() == 1, "o jogo abriu");

    printf("11. telas do SpeedBreaker\n");
    CHECK(ps_tela_cheia() == 0 && ps_ajustes_abertos() == 0, "nenhuma");
    f_modal = 1;
    CHECK(ps_tela_cheia() == 1 && ps_ajustes_abertos() == 0, "tela cheia");
    f_modal = 0;
    f_settings = 1;
    CHECK(ps_tela_cheia() == 0 && ps_ajustes_abertos() == 1, "ajustes");
    f_settings = 0;

    printf("12. diagnóstico\n");
    ps_status_texto(buf, sizeof buf);
    CHECK(strstr(buf, "Funções do app: 13 de 13") && strstr(buf, "SDL dos controles: ligado") &&
              strstr(buf, "Controle virtual: conectado (id 41)") && strstr(buf, "Aberto pelo jogo: sim") &&
              strstr(buf, "Outros controles: 1") && !strstr(buf, "erro"),
          "texto: [%s]", buf);
    char pequeno[12];
    ps_status_texto(pequeno, sizeof pequeno);
    CHECK(strlen(pequeno) == sizeof pequeno - 1, "buffer pequeno: corta sem estourar");

    printf("13. desconectar e conectar de novo\n");
    ps_desconectar();
    CHECK(f_close == 1 && f_js_fechado == &f_joystick && f_detach == 1 && f_id_desligado == 41, "close e detach");
    CHECK(ps_conectado() == 0 && ps_id() == 0 && ps_aberto_pelo_jogo() == 0, "desconectado");
    ps_rumble(&grave, &agudo, &pedidos);
    CHECK(grave == 0 && agudo == 0 && pedidos == 2, "rumble zerado (os pedidos continuam contados)");
    CHECK(ps_outros_controles() == 1, "o controle de verdade continua contando");
    zerar_contas();
    enviar(estado(1, 1, 0, 0, 0, 1, 1));
    CHECK(f_n_eixo == 0 && f_n_botao == 0, "desconectado: não manda nada");
    ps_desconectar();
    CHECK(f_close == 1 && f_detach == 1, "desconectar de novo não faz nada");
    zerar_contas();
    CHECK(ps_conectar() == 1 && ps_id() == 42 && f_attach == 2, "conectou de novo, id 42");
    CHECK(f_n_eixo == 6 && f_n_botao == 14 && f_eixo[SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] == -32768,
          "manda o neutro inteiro de novo");

    printf("14. falhas ao conectar\n");
    ps_desconectar();
    f_attach_falha = 1;
    CHECK(ps_conectar() == 0 && ps_conectado() == 0 && f_open == 2, "attach falhou: não abre");
    ps_status_texto(buf, sizeof buf);
    CHECK(strstr(buf, "Último erro: SDL_AttachVirtualJoystick: erro de mentira") != NULL, "erro no diagnóstico");
    f_attach_falha = 0;
    f_open_falha = 1;
    const int detach_antes = f_detach;
    CHECK(ps_conectar() == 0 && ps_conectado() == 0 && f_detach == detach_antes + 1, "open falhou: desfaz o attach");
    f_open_falha = 0;
    CHECK(ps_conectar() == 1, "conectou");
    ps_status_texto(buf, sizeof buf);
    CHECK(strstr(buf, "erro") == NULL, "o erro some quando conecta");

    printf("15. o SDL dos controles foi desligado\n");
    f_ligado = SDL_INIT_VIDEO;
    zerar_contas();
    const int close_antes = f_close;
    enviar(estado(1, 1, 0, 0, 0, 0, 0));
    CHECK(f_n_eixo == 0 && f_n_botao == 0 && ps_conectado() == 0, "esquece o controle sem chamar o SDL");
    CHECK(f_close == close_antes, "não fecha o que o SDL já fechou");
    CHECK(ps_outros_controles() == 0, "outros: 0");

    printf("\n%d checagens, %d falha(s)\n", checagens, falhas);
    return falhas ? 1 : 0;
}
#endif
