# litesvm-cxx

A C ABI for [LiteSVM](https://github.com/LiteSVM/litesvm)

| platform       | link with                                          |
| -------------- | -------------------------------------------------- |
| `darwin-arm64` | `-liconv -lm`                                      |
| `darwin-x64`   | `-liconv -lm`                                      |
| `linux-x64`    | `-lgcc_s -lutil -lrt -lpthread -lm -ldl`           |
| `linux-arm64`  | `-lgcc_s -lutil -lrt -lpthread -lm -ldl`           |

## Example

```c
#include "litesvm.h"

uint8_t program_id[32] = {0xC0}, counter[32] = {/* ... */};

LiteSvmHandle *svm = litesvm_new();
litesvm_add_program_from_file(svm, program_id, (const uint8_t *)"program.so", 10);

uint8_t payer_seed[32] = {1}, payer[32];
litesvm_pubkey_from_seed(payer_seed, payer);
litesvm_airdrop(svm, payer, 1000000000);

uint8_t data[] = {1};
LiteSvmAccountMeta metas[] = {{counter, false, true}, {payer, true, false}};
LiteSvmInstruction ix = {program_id, metas, 2, data, sizeof data};

LiteSvmTxOutcome *out = litesvm_send_instructions(svm, &ix, 1, payer_seed, 1);
LiteSvmTxError err;
litesvm_tx_outcome_error_info(out, &err);
if (err.kind == LITESVM_ERR_INSTRUCTION)
    printf("ix %u failed: program error %llu\n", err.instruction_index,
           (unsigned long long)err.program_error);
litesvm_tx_outcome_free(out);
litesvm_free(svm);
```

```sh
cc -Ipath/to/include test.c path/to/libs/darwin-arm64/liblitesvm.a -liconv -lm
```