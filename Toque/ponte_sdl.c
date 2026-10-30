/*
 * Toque: ponte com o SDL3 e o runtime do SpeedBreaker (ver ponte_sdl.h).
 *
 * O controle virtual é criado como o próprio runtime cria os dele nos testes (runtime/hid/input.cpp,
 * PlugVirtualPad): tipo gamepad, com todos os eixos e botões do SDL, na ordem do SDL_GamepadAxis e do
 * SDL_GamepadButton. Os gatilhos ocupam o eixo inteiro: -32768 é solto (o mesmo comentário do runtime).
 */
#include "ponte_sdl.h"

#include <SDL3/SDL_error.h>
#include <SDL3/SDL_gamepad.h>
#include <SDL3/SDL_init.h>
#include <SDL3/SDL_joystick.h>
#include <SDL3/SDL_stdinc.h>

#include <dlfcn.h>
#include <math.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#ifdef RTLD_MAIN_ONLY
#define PS_ONDE RTLD_MAIN_ONLY /* iOS: só o executável do app */
#else
#define PS_ONDE RTLD_DEFAULT /* testes em Linux */
#endif

static __typeof__(SDL_WasInit) *p_WasInit;
static __typeof__(SDL_AttachVirtualJoystick) *p_Attach;
static __typeof__(SDL_DetachVirtualJoystick) *p_Detach;
static __typeof__(SDL_OpenJoystick) *p_Open;
static __typeof__(SDL_CloseJoystick) *p_Close;
static __typeof__(SDL_SetJoystickVirtualAxis) *p_Axis;
static __typeof__(SDL_SetJoystickVirtualButton) *p_Button;
static __typeof__(SDL_GetGamepadFromID) *p_GamepadFromID;
static __typeof__(SDL_GetGamepads) *p_GetGamepads;
static __typeof__(SDL_free) *p_free;
static __typeof__(SDL_GetError) *p_GetError;
static bool (*p_SettingsOpen)(void); /* ui::SettingsOpen() */
static bool (*p_HasModal)(void);     /* ui::HasModal() */

/* A lista que o scripts/injetar.py confere no executável antes de pôr a camada no app: um nome por linha,
 * no formato { "nome", ... }. */
static const struct {
    const char *nome;
    void **onde;
} FUNCOES[] = {
    { "SDL_WasInit", (void **)&p_WasInit },
    { "SDL_AttachVirtualJoystick", (void **)&p_Attach },
    { "SDL_DetachVirtualJoystick", (void **)&p_Detach },
    { "SDL_OpenJoystick", (void **)&p_Open },
    { "SDL_CloseJoystick", (void **)&p_Close },
    { "SDL_SetJoystickVirtualAxis", (void **)&p_Axis },
    { "SDL_SetJoystickVirtualButton", (void **)&p_Button },
    { "SDL_GetGamepadFromID", (void **)&p_GamepadFromID },
    { "SDL_GetGamepads", (void **)&p_GetGamepads },
    { "SDL_free", (void **)&p_free },
    { "SDL_GetError", (void **)&p_GetError },
    { "_ZN2ui12SettingsOpenEv", (void **)&p_SettingsOpen },
    { "_ZN2ui8HasModalEv", (void **)&p_HasModal },
};
#define N_FUNCOES ((int)(sizeof FUNCOES / sizeof FUNCOES[0]))

static int g_achadas = -1;
static char g_faltando[512];
static SDL_JoystickID g_id;
static SDL_Joystick *g_js;
static char g_erro[160]; /* o último erro do SDL ao conectar */

/* último estado enviado (para mandar só o que mudou); -99999 = nunca enviado */
static int g_eixo[SDL_GAMEPAD_AXIS_COUNT];
static int g_botao[TQ_O_BOTOES];

static _Atomic uint32_t g_rumble; /* grave << 16 | agudo */
static _Atomic uint32_t g_rumble_pedidos;

/* tq_saida (botões) -> SDL_GamepadButton */
static const int BOTAO_SDL[TQ_O_BOTOES] = {
    SDL_GAMEPAD_BUTTON_SOUTH, SDL_GAMEPAD_BUTTON_EAST, SDL_GAMEPAD_BUTTON_WEST, SDL_GAMEPAD_BUTTON_NORTH,
    SDL_GAMEPAD_BUTTON_LEFT_SHOULDER, SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER, SDL_GAMEPAD_BUTTON_BACK,
    SDL_GAMEPAD_BUTTON_START, SDL_GAMEPAD_BUTTON_LEFT_STICK, SDL_GAMEPAD_BUTTON_RIGHT_STICK,
    SDL_GAMEPAD_BUTTON_DPAD_UP, SDL_GAMEPAD_BUTTON_DPAD_DOWN, SDL_GAMEPAD_BUTTON_DPAD_LEFT,
    SDL_GAMEPAD_BUTTON_DPAD_RIGHT,
};

int ps_carregar(void)
{
    if (g_achadas >= 0)
        return g_achadas == N_FUNCOES;
    g_achadas = 0;
    g_faltando[0] = 0;
    for (int i = 0; i < N_FUNCOES; i++) {
        *FUNCOES[i].onde = dlsym(PS_ONDE, FUNCOES[i].nome);
        if (*FUNCOES[i].onde)
            g_achadas++;
        else {
            if (g_faltando[0])
                strncat(g_faltando, " ", sizeof g_faltando - strlen(g_faltando) - 1);
            strncat(g_faltando, FUNCOES[i].nome, sizeof g_faltando - strlen(g_faltando) - 1);
        }
    }
    return g_achadas == N_FUNCOES;
}

int ps_funcoes_achadas(void) { return g_achadas < 0 ? 0 : g_achadas; }
int ps_funcoes_total(void) { return N_FUNCOES; }
const char *ps_faltando(void) { return g_faltando; }

static int completo(void) { return g_achadas == N_FUNCOES; }

int ps_sdl_pronto(void) { return completo() && p_WasInit(SDL_INIT_GAMEPAD) != 0; }

static bool SDLCALL ps_rumble_cb(void *userdata, Uint16 grave, Uint16 agudo)
{
    (void)userdata;
    atomic_store(&g_rumble, ((uint32_t)grave << 16) | agudo);
    atomic_fetch_add(&g_rumble_pedidos, 1);
    return true;
}

static void guardar_erro(const char *onde)
{
    const char *e = p_GetError();
    snprintf(g_erro, sizeof g_erro, "%s: %s", onde, e && *e ? e : "sem detalhe");
}

int ps_conectar(void)
{
    if (g_js)
        return 1;
    if (!ps_sdl_pronto())
        return 0;
    SDL_VirtualJoystickDesc desc;
    SDL_INIT_INTERFACE(&desc);
    desc.type = SDL_JOYSTICK_TYPE_GAMEPAD;
    desc.naxes = SDL_GAMEPAD_AXIS_COUNT;
    desc.nbuttons = SDL_GAMEPAD_BUTTON_COUNT;
    desc.name = "Toque (NFS Carbon)";
    desc.Rumble = ps_rumble_cb;
    g_id = p_Attach(&desc);
    if (!g_id) {
        guardar_erro("SDL_AttachVirtualJoystick");
        return 0;
    }
    g_js = p_Open(g_id);
    if (!g_js) {
        guardar_erro("SDL_OpenJoystick");
        p_Detach(g_id);
        g_id = 0;
        return 0;
    }
    g_erro[0] = 0;
    for (int i = 0; i < SDL_GAMEPAD_AXIS_COUNT; i++)
        g_eixo[i] = -99999;
    for (int i = 0; i < TQ_O_BOTOES; i++)
        g_botao[i] = -1;
    atomic_store(&g_rumble, 0);
    tq_estado_controle neutro;
    memset(&neutro, 0, sizeof neutro);
    ps_enviar(&neutro); /* gatilhos soltos (-32768) antes de o jogo abrir o controle */
    return 1;
}

void ps_desconectar(void)
{
    if (!g_js)
        return;
    p_Close(g_js);
    p_Detach(g_id);
    g_js = NULL;
    g_id = 0;
    atomic_store(&g_rumble, 0);
}

int ps_conectado(void) { return g_js != NULL; }
uint32_t ps_id(void) { return (uint32_t)g_id; }
int ps_aberto_pelo_jogo(void) { return g_js && p_GamepadFromID(g_id) != NULL; }

int ps_outros_controles(void)
{
    if (!ps_sdl_pronto())
        return 0;
    int n = 0, outros = 0;
    SDL_JoystickID *ids = p_GetGamepads(&n);
    for (int i = 0; ids && i < n; i++)
        if (ids[i] != g_id)
            outros++;
    if (ids)
        p_free(ids);
    return outros;
}

/* -1..1 -> -32767..32767 */
static int eixo(float v)
{
    if (v != v)
        v = 0; /* NaN */
    if (v > 1)
        v = 1;
    if (v < -1)
        v = -1;
    return (int)lroundf(v * 32767.0f);
}

/* 0..1 -> -32768..32767 (o gatilho solto é -32768) */
static int gatilho(float v)
{
    if (v != v)
        v = 0;
    if (v < 0)
        v = 0;
    if (v > 1)
        v = 1;
    return (int)lroundf(v * 65535.0f - 32768.0f);
}

void ps_enviar(const tq_estado_controle *s)
{
    if (!g_js)
        return;
    if (p_WasInit(SDL_INIT_GAMEPAD) == 0) {
        /* o SDL dos controles foi desligado e o controle virtual sumiu com ele */
        g_js = NULL;
        g_id = 0;
        return;
    }
    const int eixos[SDL_GAMEPAD_AXIS_COUNT] = {
        [SDL_GAMEPAD_AXIS_LEFTX] = eixo(s->lx),
        [SDL_GAMEPAD_AXIS_LEFTY] = eixo(s->ly),
        [SDL_GAMEPAD_AXIS_RIGHTX] = eixo(s->rx),
        [SDL_GAMEPAD_AXIS_RIGHTY] = eixo(s->ry),
        [SDL_GAMEPAD_AXIS_LEFT_TRIGGER] = gatilho(s->lt),
        [SDL_GAMEPAD_AXIS_RIGHT_TRIGGER] = gatilho(s->rt),
    };
    for (int i = 0; i < SDL_GAMEPAD_AXIS_COUNT; i++)
        if (eixos[i] != g_eixo[i] && p_Axis(g_js, i, (Sint16)eixos[i]))
            g_eixo[i] = eixos[i];
    for (int i = 0; i < TQ_O_BOTOES; i++) {
        const int b = (int)((s->botoes >> i) & 1u);
        if (b != g_botao[i] && p_Button(g_js, BOTAO_SDL[i], b != 0))
            g_botao[i] = b;
    }
}

void ps_rumble(uint16_t *grave, uint16_t *agudo, uint32_t *pedidos)
{
    const uint32_t r = atomic_load(&g_rumble);
    *grave = (uint16_t)(r >> 16);
    *agudo = (uint16_t)(r & 0xFFFF);
    *pedidos = atomic_load(&g_rumble_pedidos);
}

int ps_ajustes_abertos(void) { return completo() && p_SettingsOpen(); }
int ps_tela_cheia(void) { return completo() && p_HasModal(); }

void ps_status_texto(char *buf, size_t n)
{
    if (!completo()) {
        snprintf(buf, n, "Funções do app: %d de %d. Faltando: %s", ps_funcoes_achadas(), N_FUNCOES, g_faltando);
        return;
    }
    snprintf(buf, n,
             "Funções do app: %d de %d\n"
             "SDL dos controles: %s\n"
             "Controle virtual: %s (id %u)%s%s\n"
             "Aberto pelo jogo: %s\n"
             "Outros controles: %d\n"
             "Tela cheia do SpeedBreaker na frente: %s\n"
             "Ajustes do SpeedBreaker abertos: %s",
             g_achadas, N_FUNCOES, ps_sdl_pronto() ? "ligado" : "ainda não", g_js ? "conectado" : "desconectado",
             (unsigned)g_id, g_erro[0] ? "\nÚltimo erro: " : "", g_erro, ps_aberto_pelo_jogo() ? "sim" : "não",
             ps_outros_controles(), ps_tela_cheia() ? "sim" : "não", ps_ajustes_abertos() ? "sim" : "não");
}
