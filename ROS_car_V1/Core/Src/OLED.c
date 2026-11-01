#include "OLED.h"
#include "i2c.h"
#include <string.h>

#define OLED_CMD_CONTROL   0x00U
#define OLED_DATA_CONTROL  0x40U

#define OLED_CHUNK 32U
static uint8_t framebuffer[8][OLED_WIDTH];
static uint8_t sent[8][OLED_WIDTH];
static uint8_t sent_valid[8][OLED_WIDTH / 8U];
static uint8_t command[4], payload[OLED_CHUNK + 1U];
static const uint8_t init_commands[] = {0x00,0xAE,0x20,0x02,0xB0,0xC8,0x00,0x10,
  0x40,0x81,0x7F,0xA1,0xA6,0xA8,0x3F,0xA4,0xD3,0x00,0xD5,0x80,
  0xD9,0xF1,0xDA,0x12,0xDB,0x40,0x8D,0x14}; /* Deliberately no AF. */
enum { OLED_POSITION = 1, OLED_PIXELS, OLED_SWITCH, OLED_CONFIG };
static volatile uint8_t completed;
static uint8_t active, stage, need_data, desired_on, actual_on, online, aborting;
static uint8_t pending_page, pending_column, pending_size;
static uint16_t scan_position;
static uint32_t started_ms, retry_ms;
static uint8_t retry_wait;
static uint8_t framebuffer_dirty;
static OLED_Statistics_t statistics;

static const uint8_t *OLED_GetFont5x7(char ch)
{
  static const uint8_t space[5] = {0x00, 0x00, 0x00, 0x00, 0x00};
  static const uint8_t question[5] = {0x02, 0x01, 0x51, 0x09, 0x06};
  static const uint8_t minus[5] = {0x08, 0x08, 0x08, 0x08, 0x08};
  static const uint8_t colon[5] = {0x00, 0x36, 0x36, 0x00, 0x00};
  static const uint8_t dot[5] = {0x00, 0x60, 0x60, 0x00, 0x00};
  static const uint8_t font_0[5] = {0x3E, 0x51, 0x49, 0x45, 0x3E};
  static const uint8_t font_1[5] = {0x00, 0x42, 0x7F, 0x40, 0x00};
  static const uint8_t font_2[5] = {0x42, 0x61, 0x51, 0x49, 0x46};
  static const uint8_t font_3[5] = {0x21, 0x41, 0x45, 0x4B, 0x31};
  static const uint8_t font_4[5] = {0x18, 0x14, 0x12, 0x7F, 0x10};
  static const uint8_t font_5[5] = {0x27, 0x45, 0x45, 0x45, 0x39};
  static const uint8_t font_6[5] = {0x3C, 0x4A, 0x49, 0x49, 0x30};
  static const uint8_t font_7[5] = {0x01, 0x71, 0x09, 0x05, 0x03};
  static const uint8_t font_8[5] = {0x36, 0x49, 0x49, 0x49, 0x36};
  static const uint8_t font_9[5] = {0x06, 0x49, 0x49, 0x29, 0x1E};
  static const uint8_t font_a[5] = {0x7E, 0x11, 0x11, 0x11, 0x7E};
  static const uint8_t font_b[5] = {0x7F, 0x49, 0x49, 0x49, 0x36};
  static const uint8_t font_c[5] = {0x3E, 0x41, 0x41, 0x41, 0x22};
  static const uint8_t font_d[5] = {0x7F, 0x41, 0x41, 0x22, 0x1C};
  static const uint8_t font_e[5] = {0x7F, 0x49, 0x49, 0x49, 0x41};
  static const uint8_t font_f[5] = {0x7F, 0x09, 0x09, 0x09, 0x01};
  static const uint8_t font_g[5] = {0x3E, 0x41, 0x49, 0x49, 0x7A};
  static const uint8_t font_h[5] = {0x7F, 0x08, 0x08, 0x08, 0x7F};
  static const uint8_t font_i[5] = {0x00, 0x41, 0x7F, 0x41, 0x00};
  static const uint8_t font_k[5] = {0x7F, 0x08, 0x14, 0x22, 0x41};
  static const uint8_t font_l[5] = {0x7F, 0x40, 0x40, 0x40, 0x40};
  static const uint8_t font_m[5] = {0x7F, 0x02, 0x0C, 0x02, 0x7F};
  static const uint8_t font_n[5] = {0x7F, 0x04, 0x08, 0x10, 0x7F};
  static const uint8_t font_o[5] = {0x3E, 0x41, 0x41, 0x41, 0x3E};
  static const uint8_t font_p[5] = {0x7F, 0x09, 0x09, 0x09, 0x06};
  static const uint8_t font_r[5] = {0x7F, 0x09, 0x19, 0x29, 0x46};
  static const uint8_t font_s[5] = {0x46, 0x49, 0x49, 0x49, 0x31};
  static const uint8_t font_t[5] = {0x01, 0x01, 0x7F, 0x01, 0x01};
  static const uint8_t font_u[5] = {0x3F, 0x40, 0x40, 0x40, 0x3F};
  static const uint8_t font_v[5] = {0x1F, 0x20, 0x40, 0x20, 0x1F};
  static const uint8_t font_w[5] = {0x7F, 0x20, 0x18, 0x20, 0x7F};
  static const uint8_t font_x[5] = {0x63, 0x14, 0x08, 0x14, 0x63};
  static const uint8_t font_y[5] = {0x07, 0x08, 0x70, 0x08, 0x07};
  static const uint8_t font_z[5] = {0x61, 0x51, 0x49, 0x45, 0x43};

  switch (ch)
  {
    case ' ': return space;
    case '-': return minus;
    case ':': return colon;
    case '.': return dot;
    case '0': return font_0;
    case '1': return font_1;
    case '2': return font_2;
    case '3': return font_3;
    case '4': return font_4;
    case '5': return font_5;
    case '6': return font_6;
    case '7': return font_7;
    case '8': return font_8;
    case '9': return font_9;
    case 'A': case 'a': return font_a;
    case 'B': case 'b': return font_b;
    case 'C': case 'c': return font_c;
    case 'D': case 'd': return font_d;
    case 'E': case 'e': return font_e;
    case 'F': case 'f': return font_f;
    case 'G': case 'g': return font_g;
    case 'H': case 'h': return font_h;
    case 'I': case 'i': return font_i;
    case 'K': case 'k': return font_k;
    case 'L': case 'l': return font_l;
    case 'M': case 'm': return font_m;
    case 'N': case 'n': return font_n;
    case 'O': case 'o': return font_o;
    case 'P': case 'p': return font_p;
    case 'R': case 'r': return font_r;
    case 'S': case 's': return font_s;
    case 'T': case 't': return font_t;
    case 'U': case 'u': return font_u;
    case 'V': case 'v': return font_v;
    case 'W': case 'w': return font_w;
    case 'X': case 'x': return font_x;
    case 'Y': case 'y': return font_y;
    case 'Z': case 'z': return font_z;
    default:  return question;
  }
}
void OLED_Init(void)
{
  HAL_Delay(100);
  memset(framebuffer, 0, sizeof(framebuffer));
  memset(sent, 0xFF, sizeof(sent));
  memset(sent_valid, 0, sizeof(sent_valid));
  memset(&statistics, 0, sizeof(statistics));
  desired_on = actual_on = active = need_data = completed = aborting = 0U;
  scan_position = 0U;
  framebuffer_dirty = 1U;
  /* One startup-only transaction. Panel stays OFF through calibration/menu init. */
  online = (HAL_I2C_Master_Transmit(&hi2c1, OLED_I2C_ADDR,
              (uint8_t *)init_commands, sizeof(init_commands), 3U) == HAL_OK);
  retry_wait = 0U;
}
void OLED_DisplayOn(void) { desired_on = 1U; framebuffer_dirty = 1U; }
void OLED_DisplayOff(void) { desired_on = 0U; }
void OLED_Clear(void) { memset(framebuffer, 0, sizeof(framebuffer)); framebuffer_dirty = 1U; }
void OLED_SetCursor(uint8_t page, uint8_t column) { (void)page; (void)column; }

static void OLED_DrawChar(uint8_t page, uint8_t column, char ch, uint8_t inverted)
{
  if ((page >= 8U) || (column > OLED_WIDTH - 6U)) return;
  const uint8_t *font = OLED_GetFont5x7(ch);
  for (uint8_t i = 0U; i < 6U; ++i)
  {
    uint8_t value = i < 5U ? font[i] : 0U;
    value = inverted != 0U ? (uint8_t)~value : value;
    if (framebuffer[page][column + i] != value) framebuffer_dirty = 1U;
    framebuffer[page][column + i] = value;
  }
}
void OLED_ShowChar(uint8_t page, uint8_t column, char ch) { OLED_DrawChar(page, column, ch, 0U); }
void OLED_ShowCharInverted(uint8_t page, uint8_t column, char ch) { OLED_DrawChar(page, column, ch, 1U); }

static void OLED_DrawString(uint8_t page, uint8_t column, const char *text, uint8_t inverted)
{
  if ((text == NULL) || (page >= 8U)) return;
  while ((*text != '\0') && (column <= OLED_WIDTH - 6U))
  {
    OLED_DrawChar(page, column, *text++, inverted);
    column = (uint8_t)(column + 6U);
  }
}
void OLED_ShowString(uint8_t page, uint8_t column, const char *text) { OLED_DrawString(page, column, text, 0U); }
void OLED_ShowStringInverted(uint8_t page, uint8_t column, const char *text) { OLED_DrawString(page, column, text, 1U); }
void OLED_ClearLine(uint8_t page)
{
  if (page < 8U) { memset(framebuffer[page], 0, OLED_WIDTH); framebuffer_dirty = 1U; }
}

uint8_t OLED_I2CAvailable(void)
{
  return (uint8_t)((active == 0U) && (HAL_I2C_GetState(&hi2c1) == HAL_I2C_STATE_READY) &&
                   (__HAL_I2C_GET_FLAG(&hi2c1, I2C_FLAG_BUSY) == RESET));
}

static void OLED_Fault(uint32_t now)
{
  ++statistics.errors;
  active = completed = need_data = online = actual_on = aborting = 0U;
  retry_ms = now;
  retry_wait = 1U;
  memset(sent, 0xFF, sizeof(sent));
  memset(sent_valid, 0, sizeof(sent_valid));
  framebuffer_dirty = 1U;
}

static void OLED_Start(uint8_t *bytes, uint16_t size, uint8_t next_stage, uint32_t now)
{
  stage = next_stage;
  completed = 0U;
  active = 1U;
  started_ms = now;
  if (HAL_I2C_Master_Transmit_DMA(&hi2c1, OLED_I2C_ADDR, bytes, size) != HAL_OK)
    OLED_Fault(now);
}

void OLED_Process(uint32_t now)
{
  if (active != 0U)
  {
    if (completed != 0U)
    {
      if (completed != 1U) { OLED_Fault(now); return; }
      if (stage == OLED_POSITION) need_data = 1U;
      else if (stage == OLED_PIXELS)
      {
        /* Commit exactly the DMA snapshot, not a framebuffer changed meanwhile. */
        memcpy(&sent[pending_page][pending_column], &payload[1], pending_size);
        for (uint8_t i = 0U; i < pending_size; ++i)
        {
          uint8_t column = (uint8_t)(pending_column + i);
          sent_valid[pending_page][column / 8U] |= (uint8_t)(1U << (column % 8U));
        }
        ++statistics.chunks;
      }
      else if (stage == OLED_SWITCH) actual_on = (uint8_t)(command[1] == 0xAFU);
      else if (stage == OLED_CONFIG) { online = 1U; actual_on = 0U; }
      completed = active = aborting = 0U;
      return; /* Leave a main-loop turn for the other I2C clients. */
    }
    if ((uint32_t)(now - started_ms) >= 5U && aborting == 0U)
    {
      aborting = 1U;
      (void)HAL_I2C_Master_Abort_IT(&hi2c1, OLED_I2C_ADDR);
    }
    if ((uint32_t)(now - started_ms) >= 10U)
    {
      /* A lost IRQ or stuck transaction must not own the shared bus forever.
       * This recovery is main-only; DMA is stopped before buffer reuse. */
      (void)HAL_I2C_DeInit(&hi2c1);
      (void)HAL_I2C_Init(&hi2c1);
      ++statistics.recoveries;
      OLED_Fault(now);
    }
    return;
  }
  if (OLED_I2CAvailable() == 0U) return; /* Avoid HAL's BUSY polling on a stuck bus. */
  if (retry_wait != 0U && (uint32_t)(now - retry_ms) < 1000U) return;
  retry_wait = 0U;
  if (online == 0U)
  {
    /* After an error the physical panel state is unknown. Reapply AE even
     * when the user asked to turn it off; never leave an old AF latched. */
    OLED_Start((uint8_t *)init_commands, sizeof(init_commands), OLED_CONFIG, now);
    return;
  }
  if (desired_on == 0U)
  {
    need_data = 0U;
    if (actual_on != 0U)
    {
      command[0] = 0U; command[1] = 0xAEU;
      OLED_Start(command, 2U, OLED_SWITCH, now);
    }
    return;
  }
  if (need_data != 0U)
  {
    need_data = 0U;
    OLED_Start(payload, (uint16_t)(pending_size + 1U), OLED_PIXELS, now);
    return;
  }
  if (framebuffer_dirty == 0U && actual_on != 0U) return;
  for (uint16_t searched = 0U; searched < 8U * OLED_WIDTH; ++searched)
  {
    uint16_t pos = (uint16_t)((scan_position + searched) % (8U * OLED_WIDTH));
    uint8_t page = (uint8_t)(pos / OLED_WIDTH), column = (uint8_t)(pos % OLED_WIDTH);
    if ((sent_valid[page][column / 8U] & (1U << (column % 8U))) != 0U &&
        framebuffer[page][column] == sent[page][column]) continue;
    pending_page = page; pending_column = column; pending_size = 1U;
    for (uint8_t i = 0U; i < OLED_CHUNK && column + i < OLED_WIDTH; ++i)
      if (((sent_valid[page][(column + i) / 8U] & (1U << ((column + i) % 8U))) == 0U) ||
          framebuffer[page][column + i] != sent[page][column + i]) pending_size = (uint8_t)(i + 1U);
    payload[0] = OLED_DATA_CONTROL;
    memcpy(&payload[1], &framebuffer[page][column], pending_size);
    command[0] = OLED_CMD_CONTROL; command[1] = (uint8_t)(0xB0U + page);
    command[2] = column & 0x0FU; command[3] = (uint8_t)(0x10U | (column >> 4U));
    scan_position = (uint16_t)((pos + pending_size) % (8U * OLED_WIDTH));
    OLED_Start(command, 4U, OLED_POSITION, now);
    return;
  }
  framebuffer_dirty = 0U;
  /* Clear/upload the hidden RAM first, then turn the display on: no startup flash. */
  if (actual_on == 0U)
  {
    command[0] = 0U; command[1] = 0xAFU;
    OLED_Start(command, 2U, OLED_SWITCH, now);
  }
}

void OLED_TxCpltCallback(I2C_HandleTypeDef *i2c)
{
  if (i2c == &hi2c1 && active != 0U) completed = 1U;
}
void OLED_ErrorCallback(I2C_HandleTypeDef *i2c)
{
  if (i2c == &hi2c1 && active != 0U) completed = 2U;
}
void OLED_GetStatistics(OLED_Statistics_t *out) { if (out != NULL) *out = statistics; }


