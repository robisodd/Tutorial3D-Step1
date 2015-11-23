#include <pebble.h>
#define MAP_SIZE 100                               // Map is MAP_SIZE * MAP_SIZE squares big (each square is 64x64 pixels)
#define IDCLIP false                               // Walk thru walls if true
Window *window;                                    // The main window and canvas. We draw to root layer.
int32_t player_x = 32 * MAP_SIZE, player_y = -128; // Player's X And Y Starting Position (64 pixels per square)
int16_t player_facing = 16384;                     // Player Direction Facing [-32768 to 32767] = [-180 to 180 degrees]
uint8_t map[MAP_SIZE * MAP_SIZE];                  // The world.  0 is space, any other number is a wall

int32_t abs32(int32_t a) {return (a^(a>>31)) - (a>>31);}  // returns absolute value of A (only works on 32bit signed)

uint8_t getmap(int32_t x, int32_t y) {                                             // Returns map[] at (x,y) if in bounds, else returns 0
  x = x / 64; y = y / 64;                                                          // convert 64px_per_block to block_position
  return (x<0 || x>=MAP_SIZE || y<0 || y>=MAP_SIZE) ? 0 : map[(y * MAP_SIZE) + x]; // If within bounds, return map value, else return 0 (empty space)
}

void main_loop(void *data) {                        // The main program loop
  AccelData accel=(AccelData){.x=0, .y=0, .z=0};    // all three are int16_t
  accel_service_peek(&accel);                       // read accelerometer, use y to walk and x to rotate. discard z.
  int32_t dx = (cos_lookup(player_facing) * (accel.y>>5)) / TRIG_MAX_RATIO;  // x distance player attempts to walk
  int32_t dy = (sin_lookup(player_facing) * (accel.y>>5)) / TRIG_MAX_RATIO;  // y distance player attempts to walk
  if(getmap(player_x + dx, player_y) == 0 || IDCLIP) player_x += dx;         // If not running into wall (or no-clip is on), move x
  if(getmap(player_x, player_y + dy) == 0 || IDCLIP) player_y += dy;         // If not running into wall (or no-clip is on), move y
  player_facing += (accel.x<<3);                    // spin based on left/right tilt. note: int16_t means overflow/underflow automatically wraps.
  layer_mark_dirty(window_get_root_layer(window));  // Done updating player movement.  Tell Pebble to draw when it's ready.
  app_timer_register(50, main_loop, NULL);          // Schedule a Loop in 50ms (~20fps)
}

uint32_t shoot_ray(int32_t start_x, int32_t start_y, int32_t angle) {  // Shoots a ray from (x,y) in direction of [angle], returns distance to nearest wall at that angle.
  int32_t rx, ry, sin, cos, dx, dy, nx, ny, dist;     // ray x&y, sine & cosine, difference x&y, next x&y, ray length
  sin = sin_lookup(angle); cos = cos_lookup(angle);   // save now to make shoot_ray quicker
  rx = start_x; ry = start_y;                         // Start ray at the start
  ny = sin>0 ? 64 : -1;                               // Which side (north or south, east or west) of the square the ray_segment starts at,
  nx = cos>0 ? 64 : -1;                               //   every time the ray_segment starts.  It's determined by the direction the ray's heading.
  
  while (true) {                                      // Infinite loop, breaks out internally
    dy = ny - (ry & 63);                              // north-south component of distance to next east-west wall
    dx = nx - (rx & 63);                              // east-west component of distance to next north-south wall
    if(abs32(dx * sin) < abs32(dy * cos)) {           // if (distance to north-south wall) < (distance to east-west wall)
      rx += dx;                                       // move ray to north-south wall: x part
      ry += (dx * sin) / cos;                         // move ray to north-south wall: y part
      dist = ((rx - start_x) * TRIG_MAX_RATIO) / cos; // Distance ray traveled.  tangent = x / cos(angle)
    } else {                                          // else: (distance to Y wall) < (distance to X wall)
      rx += (dy * cos) / sin;                         // move ray to east-west wall: x part
      ry += dy;                                       // move ray to east-west wall: y part
      dist = ((ry - start_y) * TRIG_MAX_RATIO) / sin; // Distance ray traveled.  tangent = y / sin(angle)
    }                                                 // End if/then/else (x dist < y dist)
    
    if(rx>=0 && ry>=0 && rx<MAP_SIZE*64 && ry<MAP_SIZE*64) {                                          // If ray is within map bounds
      if(map[((ry>>6) * MAP_SIZE) + (rx>>6)]>0)                                                       // Check if ray hit a wall
        return dist;                                                                                  // It did! return length of ray
    } else {                                                                                          // else, ray is not within map bounds
      if((sin<=0&&ry<0) || (sin>=0&&ry>=MAP_SIZE*64) || (cos<=0&&rx<0) || (cos>=0&&rx>=MAP_SIZE*64))  // if ray is going further out of bounds
        return 0xFFFFFFFF;                                                                            // ray will never hit a wall, return infinite length
    }
  }
}

void layer_update_proc(Layer *me, GContext *ctx) {
  GRect box = layer_get_frame(me);                                      // Get size of the region to render to.  Currently the whole screen.
  //GRect box = GRect(10, 10, 60, 30);                                  // Note: It doesn't have to render full screen
  graphics_context_set_fill_color(ctx, GColorBlack);                    // Black background
  graphics_fill_rect(ctx, box, 0, GCornerNone);                         // Since we're drawing to the root layer, we need to manually blank the screen every time
  graphics_context_set_stroke_color(ctx, GColorWhite);                  // Wall color
  for(int16_t x = 0; x < box.size.w; ++x) {                             // Begin RayTracing Loop.  For every vertial column
    int16_t angle = atan2_lookup((64*x/box.size.w)-32, 64);             // Angle away from [+/-] center column: dx = (64*(col-(box.size.w/2)))/box.size.w; dy = 64; angle = atan2_lookup(dx, dy);
    int32_t dist = shoot_ray(player_x, player_y, player_facing + angle) * cos_lookup(angle);  // Shoot the ray, get distance to nearest wall.  Multiply dist by cos to stop fisheye lens.
    int16_t colheight = (box.size.h << 21) / dist;                      // wall segment height = box.size.h * wallheight * 64(the "zoom factor") / (distance >> 16) (>>16 basically = "/TRIG_MAX_RATIO")
    if(colheight>box.size.h/2) colheight=box.size.h/2;                  // Make sure line isn't drawn beyond edge
    graphics_draw_line(ctx, GPoint(x + box.origin.x, box.size.h/2 - colheight + box.origin.y), GPoint(x + box.origin.x, box.size.h/2 + colheight + box.origin.y));  // Draw the wall column at x
  } // End RayTracing Loop
}

void window_load(Window *window) {
  layer_set_update_proc(window_get_root_layer(window), layer_update_proc);     // Drawing to root layer of the window
  main_loop(NULL);                                                             // Begin looping
}

void init() {
  window = window_create();                                                    // Create window
  window_set_window_handlers(window, (WindowHandlers) {.load = window_load});  // Set window load handler
  window_stack_push(window, false);                                            // Push window to stack
  accel_data_service_subscribe(0, NULL);                                       // Start accelerometer
  srand(time(NULL));                                                           // Seed randomizer so different map every time
  for (int16_t i=0; i<MAP_SIZE*MAP_SIZE; i++)                                  // generate a randomly dotted map
    map[i] = (rand()%3==0) ? 255 : 0;                                          //   Randomly 1/3 of spots are blocks
}

void deinit() {
  accel_data_service_unsubscribe();                                            // Stop accelerometer
  window_destroy(window);                                                      // Destroy window
}

int main(void) {
  init();
  app_event_loop();
  deinit();
}