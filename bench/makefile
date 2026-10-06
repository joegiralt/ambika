VERSION        = 0.1
MCU_NAME       = 328
TARGET         = bench
PACKAGES       = avrlib avrlib/devices common bench
EXTRA_DEFINES  = -DDISABLE_DEFAULT_UART_RX_ISR -DDISABLE_WAVETABLE_LFOS $(BENCH_DEFINES)
RESOURCES      = voicecard/resources
include avrlib/makefile.mk
include $(DEP_FILE)
