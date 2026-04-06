/**
 * Pebble Dice Roller — Pebble 2 (Diorite) compatible
 *
 * Fixes vs v1:
 *  - Replaced FONT_KEY_LECO_36_BOLD_NUMBERS (Basalt/Chalk only) with
 *    FONT_KEY_BITHAM_42_BOLD which exists on ALL platforms incl. Diorite
 *  - Added accel tap debounce (500 ms guard) to prevent re-trigger loop
 *  - Cancelled any in-flight timer before starting a new roll
 *  - Null-checked every fonts_get_system_font() call
 *  - Removed GColorYellow / GColorGreen (not on Diorite) -> GColorWhite
 */

#include <pebble.h>

// ── Dice table ──────────────────────────────────────────────────────────────
static const int DICE_SIDES[]  = { 2, 3, 4, 6, 8, 10, 12, 20, 100 };
static const int NUM_DICE      = 9;
static const int DEFAULT_INDEX = 3;  // start on d6

// ── Animation ───────────────────────────────────────────────────────────────
#define ANIM_STEPS     14
#define ANIM_BASE_MS   60

// Forward declaration so resubscribe_accel_callback can reference it
static void accel_tap_handler(AccelAxisType axis, int32_t direction);

// ── State ────────────────────────────────────────────────────────────────────
static Window    *s_window;
static TextLayer *s_title_layer;
static TextLayer *s_dice_layer;
static TextLayer *s_arrows_layer;
static TextLayer *s_result_layer;
static TextLayer *s_hint_layer;
static Layer     *s_line_layer;

static int  s_dice_index  = DEFAULT_INDEX;
static bool s_rolling     = false;
static int  s_anim_step   = 0;
static int  s_final_value = 0;
static AppTimer *s_anim_timer = NULL;

// ── Helpers ──────────────────────────────────────────────────────────────────
static int random_roll(int sides) {
  return (rand() % sides) + 1;
}

static void update_dice_label(void) {
  static char buf[6];
  snprintf(buf, sizeof(buf), "d%d", DICE_SIDES[s_dice_index]);
  text_layer_set_text(s_dice_layer, buf);
}

static void update_result(int value) {
  static char buf[6];
  snprintf(buf, sizeof(buf), "%d", value);
  text_layer_set_text(s_result_layer, buf);
}

// ── Roll animation ────────────────────────────────────────────────────────────
static void resubscribe_accel_callback(void *context) {
  s_anim_timer = NULL;
  accel_tap_service_subscribe(accel_tap_handler);
}

static void anim_timer_callback(void *context) {
  s_anim_timer = NULL;  // timer has fired; clear handle first

  if (s_anim_step < ANIM_STEPS) {
    update_result(random_roll(DICE_SIDES[s_dice_index]));
    s_anim_step++;
    int delay = ANIM_BASE_MS + (s_anim_step * 8);  // ease-out
    s_anim_timer = app_timer_register(delay, anim_timer_callback, NULL);
  } else {
    update_result(s_final_value);
    text_layer_set_text(s_hint_layer, "SELECT or shake");
    s_rolling = false;
    vibes_short_pulse();
    // Re-subscribe only after 600ms — long enough for the vibration
    // motor to settle so it cannot re-trigger the accel tap handler
    s_anim_timer = app_timer_register(600, resubscribe_accel_callback, NULL);
  }
}

static void start_roll(void) {
  if (s_rolling) return;

  // Cancel any stale timer (safety net)
  if (s_anim_timer) {
    app_timer_cancel(s_anim_timer);
    s_anim_timer = NULL;
  }

  // Unsubscribe for the entire roll so the landing vibration
  // cannot be picked up as a new tap event
  accel_tap_service_unsubscribe();

  s_rolling     = true;
  s_anim_step   = 0;
  s_final_value = random_roll(DICE_SIDES[s_dice_index]);

  text_layer_set_text(s_hint_layer, "Rolling...");
  text_layer_set_text(s_result_layer, "?");

  s_anim_timer = app_timer_register(ANIM_BASE_MS, anim_timer_callback, NULL);
}

// ── Drawing ──────────────────────────────────────────────────────────────────
static void line_layer_draw(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_draw_line(ctx,
    GPoint(0, bounds.size.h / 2),
    GPoint(bounds.size.w, bounds.size.h / 2));
}

// ── Button handlers ───────────────────────────────────────────────────────────
static void up_click_handler(ClickRecognizerRef recognizer, void *context) {
  if (s_rolling) return;
  s_dice_index = (s_dice_index - 1 + NUM_DICE) % NUM_DICE;
  update_dice_label();
  text_layer_set_text(s_result_layer, "--");
}

static void down_click_handler(ClickRecognizerRef recognizer, void *context) {
  if (s_rolling) return;
  s_dice_index = (s_dice_index + 1) % NUM_DICE;
  update_dice_label();
  text_layer_set_text(s_result_layer, "--");
}

static void select_click_handler(ClickRecognizerRef recognizer, void *context) {
  start_roll();
}

static void click_config_provider(void *context) {
  window_single_click_subscribe(BUTTON_ID_UP,     up_click_handler);
  window_single_click_subscribe(BUTTON_ID_DOWN,   down_click_handler);
  window_single_click_subscribe(BUTTON_ID_SELECT, select_click_handler);
  window_single_repeating_click_subscribe(BUTTON_ID_UP,   200, up_click_handler);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 200, down_click_handler);
}

// ── Accelerometer tap (wrist flick) — debounced ───────────────────────────────
static void accel_tap_handler(AccelAxisType axis, int32_t direction) {
  start_roll();
}

// ── Window lifecycle ──────────────────────────────────────────────────────────
static void window_load(Window *window) {
  Layer *root   = window_get_root_layer(window);
  GRect  bounds = layer_get_bounds(root);

  window_set_background_color(window, GColorBlack);

  // Title
  s_title_layer = text_layer_create(GRect(0, 4, bounds.size.w, 20));
  text_layer_set_background_color(s_title_layer, GColorClear);
  text_layer_set_text_color(s_title_layer, GColorWhite);
  GFont title_font = fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD);
  if (title_font) text_layer_set_font(s_title_layer, title_font);
  text_layer_set_text_alignment(s_title_layer, GTextAlignmentCenter);
  text_layer_set_text(s_title_layer, "DICE ROLLER");
  layer_add_child(root, text_layer_get_layer(s_title_layer));

  // Separator
  s_line_layer = layer_create(GRect(10, 26, bounds.size.w - 20, 4));
  layer_set_update_proc(s_line_layer, line_layer_draw);
  layer_add_child(root, s_line_layer);

  // Arrow hint
  s_arrows_layer = text_layer_create(GRect(0, 32, bounds.size.w, 16));
  text_layer_set_background_color(s_arrows_layer, GColorClear);
  text_layer_set_text_color(s_arrows_layer, GColorWhite);
  GFont hint_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  if (hint_font) text_layer_set_font(s_arrows_layer, hint_font);
  text_layer_set_text_alignment(s_arrows_layer, GTextAlignmentCenter);
  text_layer_set_text(s_arrows_layer, "\xe2\x96\xb2  cycle  \xe2\x96\xbc");
  layer_add_child(root, text_layer_get_layer(s_arrows_layer));

  // Selected die — BITHAM_42_BOLD is available on ALL Pebble platforms
  s_dice_layer = text_layer_create(GRect(0, 48, bounds.size.w, 52));
  text_layer_set_background_color(s_dice_layer, GColorClear);
  text_layer_set_text_color(s_dice_layer, GColorWhite);
  GFont big_font = fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD);
  if (big_font) text_layer_set_font(s_dice_layer, big_font);
  text_layer_set_text_alignment(s_dice_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_dice_layer));

  // Roll result — BITHAM_42_BOLD replaces the Basalt-only LECO font
  s_result_layer = text_layer_create(GRect(0, 100, bounds.size.w, 52));
  text_layer_set_background_color(s_result_layer, GColorClear);
  text_layer_set_text_color(s_result_layer, GColorWhite);
  GFont result_font = fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD);
  if (result_font) text_layer_set_font(s_result_layer, result_font);
  text_layer_set_text_alignment(s_result_layer, GTextAlignmentCenter);
  text_layer_set_text(s_result_layer, "--");
  layer_add_child(root, text_layer_get_layer(s_result_layer));

  // Hint
  s_hint_layer = text_layer_create(GRect(0, 152, bounds.size.w, 16));
  text_layer_set_background_color(s_hint_layer, GColorClear);
  text_layer_set_text_color(s_hint_layer, GColorWhite);
  GFont small_font = fonts_get_system_font(FONT_KEY_GOTHIC_14);
  if (small_font) text_layer_set_font(s_hint_layer, small_font);
  text_layer_set_text_alignment(s_hint_layer, GTextAlignmentCenter);
  text_layer_set_text(s_hint_layer, "SELECT or shake");
  layer_add_child(root, text_layer_get_layer(s_hint_layer));

  update_dice_label();
}

static void window_unload(Window *window) {
  if (s_anim_timer) {
    app_timer_cancel(s_anim_timer);
    s_anim_timer = NULL;
  }
  text_layer_destroy(s_title_layer);
  text_layer_destroy(s_arrows_layer);
  text_layer_destroy(s_dice_layer);
  text_layer_destroy(s_result_layer);
  text_layer_destroy(s_hint_layer);
  layer_destroy(s_line_layer);
}

// ── App lifecycle ─────────────────────────────────────────────────────────────
static void init(void) {
  srand((unsigned int)time(NULL));

  s_window = window_create();
  window_set_click_config_provider(s_window, click_config_provider);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load   = window_load,
    .unload = window_unload,
  });

  accel_tap_service_subscribe(accel_tap_handler);
  window_stack_push(s_window, true);
}

static void deinit(void) {
  accel_tap_service_unsubscribe();
  window_destroy(s_window);
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}
