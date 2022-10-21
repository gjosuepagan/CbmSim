##################################
# Makefile for building: cbm_sim
# Author: Sean Gallogly
# Data last modified: 09/20/2022
##################################

ROOT      := ./
BUILD_DIR := $(ROOT)build/
SRC_DIR   := $(ROOT)src/
TARGET    := $(BUILD_DIR)cbm_sim

INC_DIRS  := $(shell find $(SRC_DIR) -type d)
INC_FLAGS := $(addprefix -I,$(INC_DIRS))

ifeq ($(shell uname -rv | awk '{print $$2}' | tr -d '#[:digit:]-'), Ubuntu)
	CUDA_PKG_NAME   := cuda-11.8
	CUDART_PKG_NAME := cudart-11.8
else
	CUDA_PKG_NAME   := cuda
	CUDART_PKG_NAME := cudart
endif

CUDA_INC_FLAGS := $(INC_FLAGS) $(shell pkg-config --cflags $(CUDA_PKG_NAME))
GTK_INC_FLAGS  := $(INC_FLAGS) $(shell pkg-config --cflags gtk+-3.0)

LIB_FLAGS := $(shell pkg-config --libs gtk+-3.0)
LIB_FLAGS += $(shell pkg-config --libs $(CUDART_PKG_NAME))

CUDA_SRCS := $(shell find $(SRC_DIR) -name "*.cu" | xargs -I {} basename {})
CUDA_OBJS := $(CUDA_SRCS:%.cu=$(BUILD_DIR)%.o)

NON_CUDA_SRCS := $(shell find $(SRC_DIR) -name "*.cpp" | xargs -I {} basename {})
NON_CUDA_OBJS := $(NON_CUDA_SRCS:%.cpp=$(BUILD_DIR)%.o)

OBJS := $(CUDA_OBJS) $(NON_CUDA_OBJS)

NVCC       := nvcc
NVCC_FLAGS := -arch=native -Xcompiler -fPIC -O3

CPP       := g++-11
CPP_FLAGS := -m64 -pipe -std=c++14 -fopenmp -O3 -fPIC

LD       := g++-11
LD_FLAGS := -m64 -fopenmp -O3

CHK_DIR_EXISTS   := test -d
MKDIR            := mkdir -p
RMDIR            := rmdir

RM               := rm -rf

VPATH := $(shell echo "${INC_DIRS}" | sed -e 's/ /:/g') 

$(BUILD_DIR)%.o: %.cu
	$(NVCC) $(NVCC_FLAGS) $(CUDA_INC_FLAGS) -c $< -o $@

$(BUILD_DIR)%.o: %.cpp
	$(CPP) $(CPP_FLAGS) $(CUDA_INC_FLAGS) $(GTK_INC_FLAGS) -c $< -o $@

$(TARGET): $(OBJS)
	$(LD) $(LD_FLAGS) $^ -o $@ $(LIB_FLAGS)

.PHONY: clean
clean:
	$(RM) $(BUILD_DIR)*

