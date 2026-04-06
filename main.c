/**
 * Pebble Dice Roller
 * - UP / DOWN buttons cycle through d2, d3, d4, d6, d8, d10, d12, d20, d100
 * - SELECT button or wrist flick (accel tap) rolls the selected die
 * - Rolling animation rapidly flickers random numbers before landing
 */

#include <pebble.h>

// ── Dice table ──────────────────────────────────────────────────────────────
static const int DICE_SIDES[]  = { 2, 3, 4, 6, 8, 10, 12, 20, 100 };
static const int NUM_DICE      = 9;
static const int DEFAULT_INDEX = 3;   // start on d6

// ── Animation settings ──────────────────────────────────────────────────────
#define ANIM_STEPS        14    // number of flicker frames
#define ANIM_INTERVAL_MS  60    // ms between frames (speeds up feel)
#define ANIM_FINAL_MS    120    // slower final pause

// ── State ────────────────────────────────────────────────────────────────────
static Window     *s_window;
static TextLayer  *s_title_layer;    // "DICE ROLLER"
static TextLayer  *s_dice_layer;     // "d20"
static TextLayer  *s_arrows_layer;   // "▲  ▼"  hint
static TextLayer  *s_result_layer;   // roll result
static TextLayer  *s_hint_layer;     // "SELECT or shake"
static Layer      *s_line_layer;     // decorative separator

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
static void anim_timer_callback(void *context) {
  s_anim_timer = NULL;

  if (s_anim_step < ANIM_STEPS) {
    // Show a random flickering number
    int flicker = random_roll(DICE_SIDES[s_dice_index]);
    update_result(flicker);
    s_anim_step++;

    // Each step gets slightly slower (ease-out feel)
    int delay = ANIM_INTERVAL_MS + (s_anim_step * 8);
    s_anim_timer = app_timer_register(delay, anim_timer_callback, NULL);
  } else {
    // Land on the real result
    update_result(s_final_value);
    text_layer_set_text(s_hint_layer, "SELECT or shake");
    s_rolling = false;

    // Vibrate briefly on landing
    vibes_short_pulse();
  }
}

static void start_roll(void) {
  if (s_rolling) return;

  s_rolling     = true;
  s_anim_step   = 0;
  s_final_value = random_roll(DICE_SIDES[s_dice_index]);

  text_layer_set_text(s_hint_layer, "Rolling...");
  text_layer_set_text(s_result_layer, "?");

  // Kick off animation
  s_anim_timer = app_timer_register(ANIM_INTERVAL_MS, anim_timer_callback, NULL);
}

// ── Drawing ──────────────────────────────────────────────────────────────────
static void line_layer_draw(Layer *layer, GContext *ctx) {
  GRect bounds = layer_get_bounds(layer);
  graphics_context_set_stroke_color(ctx, GColorWhite);
  graphics_context_set_stroke_width(ctx, 2);
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

  // Hold UP/DOWN for fast cycling
  window_single_repeating_click_subscribe(BUTTON_ID_UP,   200, up_click_handler);
  window_single_repeating_click_subscribe(BUTTON_ID_DOWN, 200, down_click_handler);
}

// ── Accelerometer tap (wrist flick) ──────────────────────────────────────────
static void accel_tap_handler(AccelAxisType axis, int32_t direction) {
  start_roll();
}

// ── Window lifecycle ──────────────────────────────────────────────────────────
static void window_load(Window *window) {
  Layer *root = window_get_root_layer(window);
  GRect  bounds = layer_get_bounds(root);

  // Dark background
  window_set_background_color(window, GColorBlack);

  // ── Title: "DICE ROLLER" ──────────────────────────────────────────────────
  s_title_layer = text_layer_create(GRect(0, 4, bounds.size.w, 22));
  text_layer_set_background_color(s_title_layer, GColorClear);
  text_layer_set_text_color(s_title_layer, GColorYellow);
  text_layer_set_font(s_title_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14_BOLD));
  text_layer_set_text_alignment(s_title_layer, GTextAlignmentCenter);
  text_layer_set_text(s_title_layer, "DICE ROLLER");
  layer_add_child(root, text_layer_get_layer(s_title_layer));

  // ── Separator line ────────────────────────────────────────────────────────
  s_line_layer = layer_create(GRect(10, 28, bounds.size.w - 20, 4));
  layer_set_update_proc(s_line_layer, line_layer_draw);
  layer_add_child(root, s_line_layer);

  // ── Arrow hints ───────────────────────────────────────────────────────────
  s_arrows_layer = text_layer_create(GRect(0, 34, bounds.size.w, 18));
  text_layer_set_background_color(s_arrows_layer, GColorClear);
  text_layer_set_text_color(s_arrows_layer, GColorLightGray);
  text_layer_set_font(s_arrows_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(s_arrows_layer, GTextAlignmentCenter);
  text_layer_set_text(s_arrows_layer, "\xe2\x96\xb2  cycle  \xe2\x96\xbc");
  layer_add_child(root, text_layer_get_layer(s_arrows_layer));

  // ── Selected die (big) ────────────────────────────────────────────────────
  s_dice_layer = text_layer_create(GRect(0, 52, bounds.size.w, 50));
  text_layer_set_background_color(s_dice_layer, GColorClear);
  text_layer_set_text_color(s_dice_layer, GColorWhite);
  text_layer_set_font(s_dice_layer, fonts_get_system_font(FONT_KEY_BITHAM_42_BOLD));
  text_layer_set_text_alignment(s_dice_layer, GTextAlignmentCenter);
  layer_add_child(root, text_layer_get_layer(s_dice_layer));

  // ── Roll result ───────────────────────────────────────────────────────────
  s_result_layer = text_layer_create(GRect(0, 106, bounds.size.w, 42));
  text_layer_set_background_color(s_result_layer, GColorClear);
  text_layer_set_text_color(s_result_layer, GColorGreen);
  text_layer_set_font(s_result_layer, fonts_get_system_font(FONT_KEY_LECO_36_BOLD_NUMBERS));
  text_layer_set_text_alignment(s_result_layer, GTextAlignmentCenter);
  text_layer_set_text(s_result_layer, "--");
  layer_add_child(root, text_layer_get_layer(s_result_layer));

  // ── Hint: "SELECT or shake" ───────────────────────────────────────────────
  s_hint_layer = text_layer_create(GRect(0, 150, bounds.size.w, 18));
  text_layer_set_background_color(s_hint_layer, GColorClear);
  text_layer_set_text_color(s_hint_layer, GColorDarkGray);
  text_layer_set_font(s_hint_layer, fonts_get_system_font(FONT_KEY_GOTHIC_14));
  text_layer_set_text_alignment(s_hint_layer, GTextAlignmentCenter);
  text_layer_set_text(s_hint_layer, "SELECT or shake");
  layer_add_child(root, text_layer_get_layer(s_hint_layer));

  // Initial dice label
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
  srand(time(NULL));  // seed RNG

  s_window = window_create();
  window_set_click_config_provider(s_window, click_config_provider);
  window_set_window_handlers(s_window, (WindowHandlers){
    .load   = window_load,
    .unload = window_unload,
  });

  // Subscribe to wrist-flick tap events
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
