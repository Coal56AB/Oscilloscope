/** @file test_encoder.c Смена направления, дребезг и восстановление фаз GPIO. */
#ifdef NDEBUG
#undef NDEBUG
#endif
#include "general_encoder.h"
#include <assert.h>
#include <stddef.h>

static GPIO_TypeDef port_a, port_b;
static const uint8_t phases[] = {0, 1, 3, 2};

/** Читать отдельные порты A/B так же, как это делает общий GPIO-драйвер. */
GPIO_PinState HAL_GPIO_ReadPin(GPIO_TypeDef *port, uint16_t pin)
{
  return (port->IDR & pin) ? GPIO_PIN_SET : GPIO_PIN_RESET;
}

/** Задать электрическое состояние, не обходя чтение GPIO внутри Encoder_Update. */
static void set_phases(uint8_t state)
{
  port_a.IDR = state & 2U;
  port_b.IDR = state & 1U;
}

/** Начальное положение GPIO не должно подменять заданную фазу фиксации. */
static void init_at(Encoder_HandleTypeDef *encoder, uint8_t state, unsigned steps, unsigned detent)
{
  set_phases(state);
  assert(Encoder_Init(encoder, &port_a, 2U, &port_b, 1U, steps, detent) == HAL_OK);
  assert(Encoder_Update(encoder) == 0);
}

/** Запустить предыдущие сценарии с явно заданным положением фиксации. */
static void init(Encoder_HandleTypeDef *encoder, uint8_t state, unsigned steps)
{
  init_at(encoder, state, steps, state);
}

/** Пройти реальные соседние фазы и вернуть сумму событий; bounce обратим. */
static int walk(Encoder_HandleTypeDef *encoder, int direction, unsigned edges, int bounce)
{
  unsigned index = 0, edge;
  int result = 0;
  while (phases[index] != encoder->state) ++index;
  for (edge = 0; edge < edges; ++edge)
  {
    uint8_t previous = phases[index];
    index = (index + (direction > 0 ? 1U : 3U)) % 4U;
    set_phases(phases[index]);
    result += Encoder_Update(encoder);
    if (bounce)
    {
      set_phases(previous);
      result += Encoder_Update(encoder);
      set_phases(phases[index]);
      result += Encoder_Update(encoder);
    }
    assert(Encoder_Update(encoder) == 0);
  }
  return result;
}

/** Каждый полный щелчок должен работать сразу после смены направления. */
static void test_reversal(unsigned steps)
{
  Encoder_HandleTypeDef encoder;
  unsigned start, i, bounce;
  for (start = 0; start < 4; ++start)
  {
    for (bounce = 0; bounce < 2; ++bounce)
    {
      init(&encoder, (uint8_t)start, steps);
      for (i = 0; i < 16; ++i) assert(walk(&encoder, 1, steps, bounce) == 1);
      assert(walk(&encoder, -1, steps, bounce) == -1);
      assert(walk(&encoder, 1, steps, bounce) == 1);
      for (i = 0; i < 16; ++i) assert(walk(&encoder, -1, steps, bounce) == -1);
      for (i = 0; i < 16; ++i)
      {
        assert(walk(&encoder, 1, steps, bounce) == 1);
        assert(walk(&encoder, -1, steps, bounce) == -1);
      }
      assert(encoder.invalid_transitions == 0);
    }
  }
}

/** Возврат незавершённого щелчка не создаёт вращение и не съедает следующий. */
static void test_partial_reversal(void)
{
  Encoder_HandleTypeDef encoder;
  unsigned start, edges;
  int direction;
  for (start = 0; start < 4; ++start)
  {
    for (edges = 1; edges < 4; ++edges)
    {
      for (direction = -1; direction <= 1; direction += 2)
      {
        init(&encoder, (uint8_t)start, 4U);
        assert(walk(&encoder, direction, edges, 1) == 0);
        assert(walk(&encoder, -direction, edges, 1) == 0);
        assert(walk(&encoder, -direction, 4U, 1) == -direction);
      }
    }
  }
}

/** Пропуск двух фаз раньше смещал счётчик и съедал первый обратный щелчок. */
static void test_resync_after_invalid_jump(void)
{
  Encoder_HandleTypeDef encoder;
  unsigned start, i;
  int direction;
  for (start = 0; start < 4; ++start)
  {
    for (direction = -1; direction <= 1; direction += 2)
    {
      init(&encoder, (uint8_t)start, 4U);
      set_phases((uint8_t)(start ^ 3U));
      assert(Encoder_Update(&encoder) == 0);
      assert(encoder.invalid_transitions == 1);
      assert(walk(&encoder, direction, 2U, 0) == 0);
      for (i = 0; i < 4; ++i) assert(walk(&encoder, direction, 4U, 0) == direction);
      assert(walk(&encoder, -direction, 4U, 0) == -direction);
      assert(walk(&encoder, direction, 4U, 0) == direction);
    }
  }
}

/** Пропуск на любом участке цикла должен терять только повреждённый детент. */
static void test_resync_every_phase(void)
{
  Encoder_HandleTypeDef encoder;
  unsigned start, before, edges;
  int direction, recovery;
  for (start = 0; start < 4; ++start)
  {
    for (before = 0; before < 4; ++before)
    {
      for (direction = -1; direction <= 1; direction += 2)
      {
        for (recovery = -1; recovery <= 1; recovery += 2)
        {
          init(&encoder, (uint8_t)start, 4U);
          assert(walk(&encoder, direction, before, 0) == 0);
          set_phases((uint8_t)(encoder.state ^ 3U));
          assert(Encoder_Update(&encoder) == 0);
          for (edges = 0; encoder.state != start; ++edges)
          {
            assert(edges < 4);
            assert(walk(&encoder, recovery, 1U, 0) == 0);
          }
          assert(walk(&encoder, recovery, 4U, 1) == recovery);
          assert(walk(&encoder, -recovery, 4U, 1) == -recovery);
          assert(encoder.invalid_transitions == 1);
        }
      }
    }
  }
}

/** После старта между щелчками первый полный реверс не должен пропадать. */
static void test_start_between_detents(void)
{
  Encoder_HandleTypeDef encoder;
  unsigned detent, initial, steps, bounce, edges, i;
  int direction;
  for (detent = 0; detent < 4; ++detent)
  {
    for (initial = 0; initial < 4; ++initial)
    {
      for (steps = 2; steps <= 4; steps += 2)
      {
        for (bounce = 0; bounce < 2; ++bounce)
        {
          for (direction = -1; direction <= 1; direction += 2)
          {
            init_at(&encoder, (uint8_t)initial, steps, detent);
            for (edges = 0; encoder.state != detent &&
                 !(steps == 2 && encoder.state == (detent ^ 3U)); ++edges)
            {
              assert(edges < 4);
              assert(walk(&encoder, direction, 1U, bounce) == 0);
            }
            assert(walk(&encoder, direction, steps, bounce) == direction);
            assert(walk(&encoder, -direction, steps, bounce) == -direction);
            for (i = 0; i < 8; ++i)
            {
              assert(walk(&encoder, direction, steps, bounce) == direction);
              assert(walk(&encoder, -direction, steps, bounce) == -direction);
            }
            assert(encoder.invalid_transitions == 0);
          }
        }
      }
    }
  }
}

/** Проверить общий декодер без модели приложения и без физической панели. */
int main(void)
{
  Encoder_HandleTypeDef encoder;
  /* Механический щелчок находится в 11, питание включено между щелчками в 00. */
  init_at(&encoder, 0U, 4U, 3U);
  assert(walk(&encoder, 1, 2U, 0) == 0);
  assert(walk(&encoder, 1, 4U, 0) == 1);
  assert(walk(&encoder, -1, 4U, 0) == -1);
  assert(Encoder_Update(NULL) == 0);
  assert(Encoder_Init(NULL, &port_a, 2U, &port_b, 1U, 4U, 3U) == HAL_ERROR);
  assert(Encoder_Init(&encoder, &port_a, 2U, &port_b, 1U, 0U, 3U) == HAL_ERROR);
  assert(Encoder_Init(&encoder, &port_a, 2U, &port_b, 1U, 4U, 4U) == HAL_ERROR);
  test_reversal(4U);
  test_reversal(2U);
  test_reversal(1U);
  test_partial_reversal();
  test_resync_after_invalid_jump();
  test_resync_every_phase();
  test_start_between_detents();
  return 0;
}
