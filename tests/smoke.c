/*
 * ensure C ABI is compatible with a simple Caravel program (ijn this case, the counter example)
 */
#include "litesvm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int failures;

#define CHECK(cond)                                                            \
    do {                                                                       \
        if (!(cond)) {                                                         \
            fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__,   \
                    #cond);                                                    \
            failures++;                                                        \
        }                                                                      \
    } while (0)

static void die_last_error(const char *what) {
    char buf[512] = {0};
    litesvm_last_error_copy((uint8_t *)buf, sizeof buf - 1);
    fprintf(stderr, "%s: %s\n", what, buf);
    exit(1);
}

static LiteSvmTxOutcome *send(LiteSvmHandle *svm, uint8_t disc,
                              LiteSvmAccountMeta *metas, size_t n,
                              const uint8_t *seeds, size_t nseeds,
                              const uint8_t *program_id) {
    LiteSvmInstruction ix = {program_id, metas, n, &disc, 1};
    LiteSvmTxOutcome *o = litesvm_send_instructions(svm, &ix, 1, seeds, nseeds);
    if (!o) die_last_error("send_instructions");
    litesvm_expire_blockhash(svm); /* identical txs would be AlreadyProcessed */
    return o;
}

static uint64_t read_count(LiteSvmHandle *svm, const uint8_t *addr) {
    LiteSvmAccount *a = litesvm_get_account(svm, addr);
    if (!a) return UINT64_MAX;
    uint8_t data[40] = {0};
    litesvm_account_data_copy(a, data, sizeof data);
    litesvm_account_free(a);
    uint64_t v;
    memcpy(&v, data, 8);
    return v;
}

int main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: %s <counter.so>\n", argv[0]);
        return 2;
    }

    CHECK(strcmp((const char *)litesvm_version(), LITESVM_CXX_VERSION) == 0);

    LiteSvmHandle *svm = litesvm_new();
    if (!svm) die_last_error("litesvm_new");

    uint8_t program_id[32], system_program[32] = {0};
    memset(program_id, 0xC0, sizeof program_id);
    if (litesvm_add_program_from_file(svm, program_id, (const uint8_t *)argv[1],
                                      strlen(argv[1])))
        die_last_error("add_program_from_file");

    uint8_t seeds[64];
    memset(seeds, 1, 32);
    memset(seeds + 32, 2, 32);
    uint8_t payer[32], counter[32];
    CHECK(litesvm_pubkey_from_seed(seeds, payer) == 0);
    CHECK(litesvm_pubkey_from_seed(seeds + 32, counter) == 0);
    CHECK(litesvm_airdrop(svm, payer, 10000000000ULL) == 0);

    /* initialize */
    LiteSvmAccountMeta init[] = {
        {payer, true, true}, {counter, true, true}, {system_program, false, false}};
    LiteSvmTxOutcome *o = send(svm, 0, init, 3, seeds, 2, program_id);
    CHECK(litesvm_tx_outcome_is_ok(o) == 1);
    CHECK(litesvm_tx_outcome_compute_units(o) > 0);
    CHECK(litesvm_tx_outcome_logs_count(o) > 0);
    LiteSvmTxError err;
    CHECK(litesvm_tx_outcome_error_info(o, &err) == 0);
    CHECK(err.kind == LITESVM_ERR_NONE);
    litesvm_tx_outcome_free(o);
    CHECK(read_count(svm, counter) == 0);

    /* increment twice, decrement once */
    LiteSvmAccountMeta inc[] = {{counter, false, true}, {payer, true, false}};
    for (int i = 0; i < 2; i++) {
        o = send(svm, 1, inc, 2, seeds, 1, program_id);
        CHECK(litesvm_tx_outcome_is_ok(o) == 1);
        litesvm_tx_outcome_free(o);
    }
    o = send(svm, 2, inc, 2, seeds, 1, program_id);
    CHECK(litesvm_tx_outcome_is_ok(o) == 1);
    litesvm_tx_outcome_free(o);
    CHECK(read_count(svm, counter) == 1);

    for (int i = 0; i < 2; i++) {
        o = send(svm, 2, inc, 2, seeds, 1, program_id);
        if (i == 0) {
            CHECK(litesvm_tx_outcome_is_ok(o) == 1);
        } else {
            CHECK(litesvm_tx_outcome_is_ok(o) == 0);
            CHECK(litesvm_tx_outcome_error_info(o, &err) == 0);
            CHECK(err.kind == LITESVM_ERR_INSTRUCTION);
            CHECK(err.instruction_index == 0);
            CHECK(err.program_error == (2ULL << 32));
        }
        litesvm_tx_outcome_free(o);
    }

    /* custom error: Caravel rejects duplicate writable accounts with
     * ERROR_CUSTOM(7) = Custom(0x107). */
    LiteSvmAccountMeta dup[] = {{counter, false, true}, {counter, false, true}};
    o = send(svm, 1, dup, 2, seeds, 1, program_id);
    CHECK(litesvm_tx_outcome_error_info(o, &err) == 0);
    CHECK(err.kind == LITESVM_ERR_INSTRUCTION);
    CHECK(err.code == LITESVM_IX_ERR_CUSTOM);
    CHECK(err.program_error == 0x107);
    litesvm_tx_outcome_free(o);

    /* running out of CUs surfaces as ProgramFailedToComplete */
    LiteSvmComputeBudget budget;
    CHECK(litesvm_get_compute_budget(svm, &budget) == 1); /* none set yet */
    CHECK(litesvm_set_compute_unit_limit(svm, 10) == 0);
    CHECK(litesvm_get_compute_budget(svm, &budget) == 0);
    CHECK(budget.compute_unit_limit == 10);
    o = send(svm, 1, inc, 2, seeds, 1, program_id);
    CHECK(litesvm_tx_outcome_error_info(o, &err) == 0);
    CHECK(err.kind == LITESVM_ERR_INSTRUCTION);
    CHECK(err.code == LITESVM_IX_ERR_PROGRAM_FAILED_TO_COMPLETE);
    CHECK(err.program_error == 0);
    litesvm_tx_outcome_free(o);

    /* missing signer seed fails to build, not to execute */
    LiteSvmAccountMeta needs_counter_sig[] = {
        {payer, true, true}, {counter, true, true}, {system_program, false, false}};
    LiteSvmInstruction ix = {program_id, needs_counter_sig, 3, (uint8_t[]){0}, 1};
    CHECK(litesvm_send_instructions(svm, &ix, 1, seeds, 1) == NULL);
    CHECK(litesvm_last_error_copy(NULL, 0) > 0);

    /* PDA: deterministic, and the bump is the highest viable one */
    const uint8_t *pda_seeds[] = {(const uint8_t *)"counter", payer};
    size_t pda_lens[] = {7, 32};
    uint8_t pda[32], pda2[32], bump = 0, bump2 = 0;
    CHECK(litesvm_find_program_address(pda_seeds, pda_lens, 2, program_id, pda,
                                       &bump) == 0);
    CHECK(litesvm_find_program_address(pda_seeds, pda_lens, 2, program_id, pda2,
                                       &bump2) == 0);
    CHECK(memcmp(pda, pda2, 32) == 0 && bump == bump2);

    LiteSvmRent rent;
    CHECK(litesvm_get_rent(svm, &rent) == 0);
    CHECK(rent.lamports_per_byte > 0);

    litesvm_free(svm);
    if (failures) {
        fprintf(stderr, "smoke: %d check(s) failed\n", failures);
        return 1;
    }
    printf("smoke: ok\n");
    return 0;
}
