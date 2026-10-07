# Contributing

Thanks for wanting to help. Bug reports, fixes and new features are all welcome.
There is no contributor agreement to sign: under section 5 of the Apache Licence
2.0, anything you submit for inclusion is covered by the project's own licence.

## Reporting a bug

Open an issue and include the STM32 family you are using, the exact flash part
number, what `spif_init()` put in `manufacturer` and `memory_type`, the
`SPIF_TRANSFER` set in your `spif_config.h`, the RTOS set in your
`osal_config.h`, the error value you got back, and the smallest piece of code
that shows the problem. A failing test is even better, see below.

## Making a change

1. Add or update a test in `test/test_spif.c` that fails before your change and
   passes after it. The HAL calls land on a model of a real chip, in the same
   file, so most chip behaviour can be tested without hardware. The model can
   be several kinds of chip, a Micron, a Spansion FL-S or an SST26 among them:
   `init_kind()` puts the one you need on the bus. A new maker's quirk belongs
   in the model first, from its datasheet, then in the driver. osal is a fake
   in `test/fake/osal.h`, implemented in the same file, so a test can hold or
   refuse the mutex. If a change cannot be covered by a test, say why in the
   pull request.
2. Run the tests:

   ```bash
   python test/run_tests.py
   ```

   This builds and runs the suite twice, once with `SPIF_TRANSFER_POLLING`,
   from `spif_config.h` as it ships, and once with `SPIF_TRANSFER_DMA`. A test
   that only means something with DMA sits inside `#if USES_DMA`. There is no
   build per RTOS: spif reaches the RTOS only through osal, and osal's own
   tests cover each one.

3. Match the existing code style. The short version: 4 spaces and no tabs, Allman
   braces, `snake_case`, every file scope name prefixed with `spif_`, a Doxygen
   block on every public function, section banners at 103 columns, and comments
   in plain 7-bit ASCII with no em dashes. A `.clang-format` in the project root
   handles the mechanical parts:

   ```bash
   clang-format -i src/spif.c src/spif.h src/spif_config.h
   ```

4. Keep vendor code out of the library. `src/spif.h` includes only the standard
   headers, `main.h` for the HAL's types, `osal.h` for the mutex and the waits,
   and its own `spif_config.h`. Nothing RTOS specific belongs in spif: it goes
   in osal. That is what lets the tests replace all of them on a PC.

5. A new setting goes in `spif_config.h`, between its `USER CODE` lines, and is
   checked in `spif.h`, never in `spif_config.h`. Give it a default in `spif.h`,
   so a configuration file from an older version still builds. The DMA test
   build and the CI cross compile skip the shipped file and give every setting
   on the command line, so add it to `test/CMakeLists.txt` and to
   `.github/workflows/ci.yml` as well.

## What CI checks

Every pull request runs two jobs, and both must be green:

- the full test suite on Ubuntu, both builds, with `-Wall -Wextra -Wpedantic -Werror`
- a cross compile of `src/spif.c` for Cortex-M4 with the same warning settings
  and `-Wundef`, against the real osal, for each RTOS setting, by polling and
  by DMA

Warnings are errors, so a build that warns will not merge.

## Questions

Open an issue, or reach me at nima.askari@gmail.com.
