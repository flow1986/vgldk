
NAME = basic

# For targeting a specific model:
#VGLDK_SERIES = 1000
#VGLDK_SERIES = 2000
#VGLDK_SERIES = 4000
VGLDK_SERIES = 6000

# A full line-numbered BASIC interpreter (tokenizer, expression parser,
# FOR/NEXT + GOSUB stacks, PEEK/POKE, PLOT/LINE, SOUND) does not fit an
# 8KB cart (AT28C64B) - use a 32KB one (AT28C256) instead.
CART_SIZE_KB = 32

all: cart
#all: emu

include ../../tools/Makefile.mk
