# SPDX-License-Identifier: GPL-2.0-only
obj-$(CONFIG_XILINX_DMATEST) += axidmatest.o
obj-$(CONFIG_XILINX_VDMATEST) += vdmatest.o
obj-$(CONFIG_XILINX_DPDMA) += xilinx_dpdma.o
obj-$(CONFIG_XILINX_DMA) += xilinx_dma.o
obj-$(CONFIG_XILINX_ZYNQMP_DMA) += zynqmp_dma.o
xilinx_ps_pcie_dma-objs := xilinx_ps_pcie_main.o xilinx_ps_pcie_platform.o
obj-$(CONFIG_XILINX_PS_PCIE_DMA) += xilinx_ps_pcie_dma.o
obj-$(CONFIG_XILINX_PS_PCIE_DMA_TEST) += xilinx_ps_pcie_dma_client.o
obj-$(CONFIG_XILINX_FRMBUF) += xilinx_frmbuf.o

# axis_dma (Shiroha)

obj-m  := axis.o

# Path to the Linux kernel, if not passed in as arg, set default.
# ifeq ($(KDIR),)
# 	KDIR := /lib/modules/$(shell uname -r)/build
# endif


# all:
# 	make -C $(KDIR) M=$(PWD) modules

# clean:
# 	make -C $(KDIR) M=$(PWD) clean

