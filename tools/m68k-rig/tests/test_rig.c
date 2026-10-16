#include "../../../tests/test.h"
#include "../rig.h"

/* Hand-assembled 68000: move.l 4(sp),d0 ; add.l 8(sp),d0 ; rts */
static const uint8_t add_fn[] = { 0x20, 0x2F, 0x00, 0x04, 0xD0, 0xAF, 0x00, 0x08, 0x4E, 0x75 };

TEST(calls_a_two_argument_function_and_returns_d0) {
    ASSERT_EQ_INT(rig_init(1 << 20), 0);
    rig_poke(0x1000, add_fn, sizeof add_fn);
    uint32_t args[2] = { 3, 4 };
    ASSERT_EQ_INT(rig_call(0x1000, args, 2), 7);
    uint32_t big[2] = { 0xFFFFFFFFu, 2 };
    ASSERT_EQ_INT(rig_call(0x1000, big, 2), 1);   /* 32-bit wraparound */
    rig_shutdown();
}

TEST(peek_reads_back_what_poke_wrote) {
    ASSERT_EQ_INT(rig_init(1 << 16), 0);
    uint8_t in[4] = { 1, 2, 3, 4 }, out[4] = { 0 };
    rig_poke(0x2000, in, 4);
    rig_peek(0x2000, out, 4);
    ASSERT_EQ_INT(memcmp(in, out, 4), 0);
    rig_shutdown();
}

TEST(loads_relocated_game_image_when_present) {
    FILE *f = fopen("../../extracted/interceptor.bin", "rb");
    if (!f) { printf("(skipped, no extracted/) "); return; }
    fclose(f);
    ASSERT_EQ_INT(rig_init(1 << 21), 0);
    ASSERT_EQ_INT(rig_load_image("../../extracted/interceptor.bin", 0x10000), 0);
    uint8_t first[2];
    rig_peek(0x10000, first, 2);
    ASSERT_TRUE(first[0] != 0 || first[1] != 0);
    rig_shutdown();
}

TEST(infinite_loop_reports_timeout) {
    static const uint8_t spin[] = { 0x60, 0xFE };   /* bra.s * */
    ASSERT_EQ_INT(rig_init(1 << 16), 0);
    rig_poke(0x1000, spin, sizeof spin);
    rig_set_max_instructions(1000);
    ASSERT_EQ_INT(rig_call(0x1000, NULL, 0), 0);
    ASSERT_EQ_INT(rig_last_error, RIG_ERR_TIMEOUT);
    rig_set_max_instructions(50000000u);
    uint32_t args[2] = { 1, 2 };
    rig_poke(0x2000, add_fn, sizeof add_fn);
    ASSERT_EQ_INT(rig_call(0x2000, args, 2), 3);
    ASSERT_EQ_INT(rig_last_error, RIG_OK);
    rig_shutdown();
}

TEST(load_image_rejects_oversize_and_bad_base) {
    ASSERT_EQ_INT(rig_load_image("/dev/null", 0), -1);   /* no rig_init yet */
    ASSERT_EQ_INT(rig_init(1 << 12), 0);
    FILE *f = fopen("test_rig_big.tmp", "wb");
    ASSERT_TRUE(f != NULL);
    for (int i = 0; i < 5000; i++) fputc(0xAA, f);
    fclose(f);
    ASSERT_EQ_INT(rig_load_image("test_rig_big.tmp", 0), -1);        /* larger than RAM */
    ASSERT_EQ_INT(rig_load_image("test_rig_big.tmp", 1 << 20), -1);  /* base out of range */
    remove("test_rig_big.tmp");
    rig_shutdown();
}

/* add.l d1,d0 ; rts */
static const uint8_t add_regs_fn[] = { 0xD0, 0x81, 0x4E, 0x75 };

TEST(run_at_address_uses_and_returns_registers) {
    ASSERT_EQ_INT(rig_init(1 << 16), 0);
    rig_poke(0x3000, add_regs_fn, sizeof add_regs_fn);
    rig_set_reg(RIG_D0, 40);
    rig_set_reg(RIG_D1, 2);
    ASSERT_EQ_INT(rig_run(0x3000), 0);
    ASSERT_EQ_INT(rig_last_error, RIG_OK);
    ASSERT_EQ_INT(rig_get_reg(RIG_D0), 42);
    ASSERT_EQ_INT(rig_get_reg(RIG_D1), 2);
    rig_shutdown();
}

TEST(run_reports_timeout_without_hanging) {
    ASSERT_EQ_INT(rig_init(1 << 16), 0);
    static const uint8_t loop_fn[] = { 0x60, 0xFE };   /* bra.s * */
    rig_poke(0x3000, loop_fn, sizeof loop_fn);
    rig_set_max_instructions(1000);
    ASSERT_TRUE(rig_run(0x3000) != 0);
    ASSERT_EQ_INT(rig_last_error, RIG_ERR_TIMEOUT);
    rig_set_max_instructions(50000000u);
    rig_shutdown();
}

TEST(run_outside_image_is_an_error) {
    ASSERT_EQ_INT(rig_init(1 << 16), 0);
    ASSERT_TRUE(rig_run(0x100000) != 0);          /* beyond RAM */
    ASSERT_EQ_INT(rig_last_error, RIG_ERR_UNEXPECTED_PC);
    rig_shutdown();
}

int main(void) {
    RUN(calls_a_two_argument_function_and_returns_d0);
    RUN(peek_reads_back_what_poke_wrote);
    RUN(loads_relocated_game_image_when_present);
    RUN(infinite_loop_reports_timeout);
    RUN(load_image_rejects_oversize_and_bad_base);
    RUN(run_at_address_uses_and_returns_registers);
    RUN(run_reports_timeout_without_hanging);
    RUN(run_outside_image_is_an_error);
    TEST_MAIN_END();
}
