#ifndef __NVKM_PCI_PRIV_H__
#define __NVKM_PCI_PRIV_H__
#define nvkm_pci(p) container_of((p), struct nvkm_pci, subdev)
#include <subdev/pci.h>

int nvkm_pci_new_(const struct nvkm_pci_func *, struct nvkm_device *,
		  int index, struct nvkm_pci **);

struct nvkm_pci_func {
	void (*init)(struct nvkm_pci *);
	u32 (*rd32)(struct nvkm_pci *, u16 addr);
	void (*wr08)(struct nvkm_pci *, u16 addr, u8 data);
	void (*wr32)(struct nvkm_pci *, u16 addr, u32 data);
	void (*msi_rearm)(struct nvkm_pci *);
};

u32 nv40_pci_rd32(struct nvkm_pci *, u16);
void nv40_pci_wr08(struct nvkm_pci *, u16, u8);
void nv40_pci_wr32(struct nvkm_pci *, u16, u32);
void nv40_pci_msi_rearm(struct nvkm_pci *);

void nv46_pci_msi_rearm(struct nvkm_pci *);

void g84_pci_init(struct nvkm_pci *pci);
#endif
