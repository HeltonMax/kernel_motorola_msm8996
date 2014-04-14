#include "common.h"

extern u32 MediaChange;
extern int Check_D_MediaFmt(struct us_data *);



static u8 SM_Init[] = {
0x7B, 0x09, 0x7C, 0xF0, 0x7D, 0x10, 0x7E, 0xE9,
0x7F, 0xCC, 0x12, 0x2F, 0x71, 0x90, 0xE9, 0xCC,
0xE0, 0xB4, 0x07, 0x12, 0x90, 0xFF, 0x09, 0xE0,
0x30, 0xE1, 0x06, 0x90, 0xFF, 0x23, 0x74, 0x80,
0xF0, 0x12, 0x2F, 0x5C, 0xD3, 0x22, 0x78, 0x00,
0x90, 0xFF, 0x83, 0xE0, 0xA2, 0xE1, 0x92, 0x0A,
0x20, 0x0A, 0x03, 0x02, 0xE0, 0xD0, 0x7F, 0x00,
0x12, 0x2F, 0xCB, 0x20, 0x01, 0x05, 0xC2, 0x25,
0x02, 0xE0, 0xEB, 0xC3, 0xE8, 0x94, 0x02, 0x40,
0x03, 0x02, 0xE0, 0xD0, 0xC0, 0x00, 0x90, 0xFE,
0x66, 0x74, 0x90, 0xF0, 0x12, 0xE1, 0x40, 0x90,
0xFF, 0x95, 0xE0, 0xC2, 0xE4, 0xF0, 0x90, 0xFF,
0x97, 0x74, 0x01, 0xF0, 0x7E, 0x01, 0x7F, 0x90,
0x12, 0x2F, 0x74, 0x90, 0xFF, 0x97, 0x74, 0x03,
0xF0, 0x90, 0xFE, 0xC5, 0xE4, 0xF0, 0x74, 0x00,
0x90, 0xFE, 0x6A, 0xF0, 0xA3, 0xF0, 0xA3, 0xF0,
0xA3, 0xF0, 0x7E, 0x23, 0x7F, 0xDC, 0x12, 0x2F,
0x74, 0x12, 0x2F, 0x5C, 0x90, 0xFE, 0x64, 0xE0,
0x54, 0x01, 0x60, 0x04, 0xD2, 0x02, 0x80, 0x02,
0xC2, 0x02, 0x90, 0xFF, 0x95, 0xE0, 0xD2, 0xE4,
0xF0, 0x78, 0x10, 0x79, 0x04, 0x12, 0xE1, 0x71,
0x50, 0x3A, 0x90, 0xE9, 0xC6, 0xE0, 0x90, 0xE9,
0xC3, 0xF0, 0x78, 0x9A, 0x79, 0x04, 0x12, 0xE1,
0x71, 0x50, 0x29, 0x90, 0xE9, 0xC7, 0xE0, 0xB4,
0xB5, 0x22, 0x90, 0xE9, 0xC4, 0xF0, 0xD0, 0x00,
0xD2, 0x00, 0xC2, 0x01, 0xC2, 0x25, 0x80, 0x1B,
0xC2, 0x00, 0xD2, 0x01, 0x74, 0xFF, 0x90, 0xE9,
0xC3, 0xF0, 0xA3, 0xF0, 0x51, 0x01, 0xC2, 0x0A,
0xC2, 0x02, 0x80, 0x07, 0xD0, 0x00, 0x05, 0x00,
0x02, 0xE0, 0x43, 0x90, 0xFF, 0x09, 0xE0, 0x30,
0xE1, 0x06, 0x90, 0xFF, 0x23, 0x74, 0x80, 0xF0,
0x90, 0xFF, 0x09, 0xE0, 0x30, 0xE5, 0xFC, 0xE4,
0xA2, 0x0A, 0x92, 0xE0, 0xA2, 0x00, 0x92, 0xE1,
0xA2, 0x01, 0x92, 0xE2, 0xA2, 0x02, 0x92, 0xE6,
0xA2, 0x25, 0x92, 0xE7, 0x90, 0xF4, 0x00, 0xF0,
0x90, 0xE9, 0xC3, 0xE0, 0x90, 0xF4, 0x01, 0xF0,
0x90, 0xE9, 0xC4, 0xE0, 0x90, 0xF4, 0x02, 0xF0,
0x90, 0xFF, 0x2A, 0x74, 0x02, 0xF0, 0xA3, 0x74,
0x00, 0xF0, 0x75, 0x3C, 0x00, 0x75, 0x3D, 0x00,
0x75, 0x3E, 0x00, 0x75, 0x3F, 0x00, 0xD3, 0x22,
0x90, 0xFE, 0x71, 0xE4, 0xF0, 0x90, 0xFE, 0x72,
0x74, 0x01, 0xF0, 0x90, 0xFE, 0x64, 0x74, 0x0C,
0xF0, 0x90, 0xFE, 0x64, 0x74, 0x00, 0x45, 0x4E,
0xF0, 0x90, 0xFE, 0x64, 0xE0, 0x54, 0x10, 0x60,
0x08, 0x90, 0xFE, 0x72, 0x74, 0x81, 0xF0, 0xD3,
0x22, 0x90, 0xFE, 0x64, 0x74, 0x08, 0xF0, 0xC3,
0x22, 0x90, 0xFE, 0x6F, 0xE9, 0x14, 0xF0, 0x90,
0xFE, 0x70, 0xE0, 0x54, 0xFC, 0xF0, 0x90, 0xFE,
0x68, 0x74, 0x00, 0xF0, 0xB8, 0x9A, 0x2A, 0x74,
0x15, 0x90, 0xFE, 0x64, 0xF0, 0x74, 0x9A, 0x90,
0xFE, 0x60, 0xF0, 0x74, 0x16, 0x90, 0xFE, 0x64,
0xF0, 0x74, 0x00, 0x90, 0xFE, 0x60, 0xF0, 0x30,
0x0A, 0x5D, 0x90, 0xFE, 0x64, 0xE0, 0x20, 0xE7,
0xF6, 0x74, 0x14, 0x90, 0xFE, 0x64, 0xF0, 0x80,
0x20, 0x90, 0xFE, 0x6E, 0xE8, 0x44, 0x01, 0xF0,
0xC2, 0x09, 0x12, 0xE3, 0x26, 0x20, 0x08, 0x0E,
0x12, 0xE3, 0x32, 0x30, 0x3E, 0xF7, 0x90, 0xFE,
0xD8, 0x74, 0x01, 0xF0, 0xD2, 0x09, 0x20, 0x09,
0x2E, 0x7A, 0xE9, 0x7B, 0xC5, 0x7C, 0xFE, 0x7D,
0x60, 0xB8, 0x10, 0x07, 0x90, 0xFE, 0x69, 0xE0,
0x20, 0xE6, 0xFC, 0x8C, 0x83, 0x8D, 0x82, 0xE0,
0x8A, 0x83, 0x8B, 0x82, 0xF0, 0xA3, 0xAA, 0x83,
0xAB, 0x82, 0xD9, 0xE5, 0xB8, 0x9A, 0x06, 0x74,
0x10, 0x90, 0xFE, 0x64, 0xF0, 0xD3, 0x22, 0xC3,
0x22, 0x90, 0xFF, 0x83, 0xE0, 0xA2, 0xE1, 0x92,
0x25, 0x20, 0x25, 0x06, 0xC2, 0x1F, 0xD2, 0x19,
0xC3, 0x22, 0x7F, 0x02, 0x12, 0x2F, 0xCB, 0x20,
0x19, 0x05, 0x30, 0x1F, 0x02, 0xD3, 0x22, 0x90,
0xEA, 0x44, 0x74, 0x80, 0xF0, 0x7F, 0x10, 0x12,
0x2F, 0xC5, 0x90, 0xFE, 0x47, 0xE0, 0x44, 0x80,
0xF0, 0x78, 0x00, 0xE8, 0xC3, 0x94, 0x04, 0x50,
0x0A, 0x7F, 0x88, 0x7E, 0x13, 0x12, 0xE3, 0x4D,
0x08, 0x80, 0xF0, 0x90, 0xFE, 0x45, 0xE0, 0x54,
0xFB, 0xF0, 0x90, 0xFE, 0x47, 0xE0, 0x54, 0xBF,
0xF0, 0x90, 0xFE, 0x45, 0xE0, 0x54, 0xFE, 0xF0,
0x90, 0xFE, 0x45, 0xE0, 0x54, 0x7F, 0xF0, 0x90,
0xFE, 0x46, 0xE0, 0x44, 0x40, 0xF0, 0x90, 0xFE,
0x45, 0xE0, 0x54, 0xC7, 0x44, 0x18, 0xF0, 0x90,
0xFE, 0x47, 0xE0, 0x44, 0x08, 0xF0, 0x90, 0xFE,
0x45, 0xE0, 0x44, 0x40, 0xF0, 0x7F, 0x32, 0x7E,
0x00, 0x12, 0xE3, 0x4D, 0x90, 0xFE, 0x51, 0xE0,
0x54, 0x33, 0xF0, 0x90, 0xFE, 0x44, 0x74, 0x02,
0xF0, 0x30, 0x25, 0x04, 0xE0, 0x20, 0xE1, 0xF9,
0x90, 0xFE, 0x51, 0xE0, 0x54, 0x0F, 0xF0, 0x90,
0xFE, 0x44, 0x74, 0x02, 0xF0, 0x30, 0x25, 0x04,
0xE0, 0x20, 0xE1, 0xF9, 0x90, 0xFE, 0x44, 0x74,
0x04, 0xF0, 0x30, 0x25, 0x04, 0xE0, 0x20, 0xE2,
0xF9, 0x90, 0xFE, 0x4C, 0xE0, 0xF0, 0x90, 0xFE,
0x4D, 0xE0, 0xF0, 0x90, 0xFE, 0x48, 0x74, 0x7F,
0xF0, 0x90, 0xFE, 0x49, 0x74, 0x9F, 0xF0, 0x90,
0xFE, 0x51, 0xE0, 0x54, 0x3C, 0x44, 0x02, 0xF0,
0x90, 0xFE, 0x44, 0x74, 0x02, 0xF0, 0x30, 0x25,
0x04, 0xE0, 0x20, 0xE1, 0xF9, 0x90, 0xFE, 0x46,
0xE0, 0x44, 0x20, 0xF0, 0x79, 0x02, 0x7A, 0x06,
0x7B, 0x00, 0x7C, 0x00, 0x7D, 0x06, 0x7E, 0xEB,
0x7F, 0xC9, 0x12, 0x2F, 0xA7, 0x40, 0x03, 0x02,
0xE3, 0x04, 0xD3, 0x22, 0xE4, 0x90, 0xFE, 0x48,
0xF0, 0x90, 0xFE, 0x49, 0xF0, 0x90, 0xFE, 0x4C,
0xE0, 0xF0, 0x90, 0xFE, 0x4D, 0xE0, 0xF0, 0x90,
0xFE, 0x47, 0xE0, 0x54, 0x7F, 0xF0, 0xC2, 0x25,
0xC2, 0x1F, 0xD2, 0x19, 0xC3, 0x22, 0xC2, 0x3E,
0x75, 0x7C, 0x00, 0x75, 0x7D, 0x00, 0x75, 0x7E,
0x00, 0x22, 0x05, 0x7C, 0xE5, 0x7C, 0x70, 0x14,
0x05, 0x7D, 0xE5, 0x7D, 0x70, 0x04, 0x05, 0x7E,
0x80, 0x0A, 0xB4, 0x17, 0x07, 0xE5, 0x7E, 0xB4,
0x06, 0x02, 0xD2, 0x3E, 0x22, 0x75, 0x8A, 0x00,
0x75, 0x8C, 0xCE, 0xC2, 0x8D, 0x90, 0xEA, 0x65,
0xE4, 0xF0, 0xA3, 0xF0, 0xD2, 0x8C, 0x90, 0xEA,
0x65, 0xE0, 0xFC, 0xA3, 0xE0, 0xFD, 0xEC, 0xC3,
0x9E, 0x40, 0xF3, 0x70, 0x05, 0xED, 0xC3, 0x9F,
0x40, 0xEC, 0xC2, 0x8C, 0x22, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x58, 0x44, 0x2D, 0x49, 0x6E, 0x69, 0x74, 0x20,
0x20, 0x20, 0x20, 0x31, 0x30, 0x30, 0x30, 0x31 };

static u8 SM_Rdwr[] = {
0x7B, 0x0C, 0x7C, 0xF0, 0x7D, 0x10, 0x7E, 0xE9,
0x7F, 0xCC, 0x12, 0x2F, 0x71, 0x90, 0xE9, 0xC3,
0xE0, 0xB4, 0x73, 0x04, 0x74, 0x40, 0x80, 0x09,
0xB4, 0x75, 0x04, 0x74, 0x40, 0x80, 0x02, 0x74,
0xC0, 0x90, 0xFE, 0x70, 0xF0, 0x90, 0xFF, 0x09,
0xE0, 0x30, 0xE1, 0x06, 0x90, 0xFF, 0x23, 0x74,
0x80, 0xF0, 0x90, 0xFF, 0x83, 0xE0, 0xA2, 0xE1,
0x92, 0x0A, 0x40, 0x01, 0x22, 0x90, 0xFE, 0x6A,
0xE4, 0xF0, 0x90, 0xE9, 0xCC, 0xE0, 0xB4, 0x02,
0x05, 0xD2, 0x06, 0x02, 0xE0, 0x78, 0xB4, 0x03,
0x03, 0x02, 0xE3, 0xD0, 0xB4, 0x04, 0x03, 0x02,
0xE1, 0xC6, 0xB4, 0x05, 0x03, 0x02, 0xE5, 0x20,
0xB4, 0x06, 0x03, 0x02, 0xE5, 0xE0, 0xB4, 0x07,
0x05, 0x12, 0x2F, 0x5C, 0xD3, 0x22, 0xB4, 0x08,
0x05, 0xC2, 0x06, 0x02, 0xE6, 0x3B, 0xC3, 0x22,
0xE5, 0x3E, 0xC3, 0x13, 0x90, 0xE9, 0xCA, 0xF0,
0xC0, 0xE0, 0x75, 0xF0, 0x02, 0xC0, 0xF0, 0x12,
0xE0, 0xD8, 0xEF, 0x70, 0x21, 0x20, 0x37, 0x07,
0x20, 0x09, 0x04, 0xD0, 0xF0, 0x80, 0x05, 0xD0,
0xF0, 0xD5, 0xF0, 0xE9, 0xD0, 0xE0, 0x90, 0xFF,
0x28, 0xE0, 0x30, 0xE7, 0xFC, 0x90, 0xFF, 0x28,
0xE0, 0x44, 0x01, 0xF0, 0xC3, 0x22, 0xD0, 0xF0,
0x90, 0xE9, 0xCF, 0xE0, 0x24, 0x01, 0xF0, 0x90,
0xE9, 0xCE, 0xE0, 0x34, 0x00, 0xF0, 0x90, 0xE9,
0xCD, 0xE0, 0x34, 0x00, 0xF0, 0xD0, 0xE0, 0x14,
0x70, 0xB6, 0x75, 0x3C, 0x00, 0x75, 0x3D, 0x00,
0x75, 0x3E, 0x00, 0x75, 0x3F, 0x00, 0xD3, 0x22,
0xC2, 0x08, 0xC2, 0x36, 0xC2, 0x37, 0xE4, 0x90,
0xEB, 0xC2, 0xF0, 0x90, 0xE9, 0xCD, 0xE0, 0xF8,
0xA3, 0xE0, 0xF9, 0xA3, 0xE0, 0x90, 0xFE, 0x6B,
0xF0, 0xA3, 0xE9, 0xF0, 0xA3, 0xE8, 0xF0, 0x90,
0xFE, 0x6F, 0x74, 0x0F, 0xF0, 0x90, 0xFE, 0x70,
0xE0, 0x54, 0xFC, 0x44, 0x02, 0xF0, 0x90, 0xFE,
0xC6, 0x74, 0x02, 0xF0, 0xA3, 0x74, 0x0F, 0xF0,
0x90, 0xFE, 0xC0, 0x74, 0xF4, 0xF0, 0x74, 0x00,
0xA3, 0xF0, 0x90, 0xFE, 0x68, 0x74, 0x21, 0xF0,
0x90, 0xFE, 0x64, 0x74, 0x70, 0x45, 0x4E, 0xF0,
0x90, 0xFE, 0x64, 0x74, 0x30, 0x45, 0x4E, 0xF0,
0x30, 0x06, 0x07, 0x90, 0xFF, 0x09, 0xE0, 0x30,
0xE5, 0xFC, 0x90, 0xFE, 0x6E, 0x74, 0x51, 0xF0,
0x90, 0xFE, 0xC4, 0x74, 0x21, 0xF0, 0xC2, 0x09,
0x12, 0xE7, 0xB0, 0x20, 0x08, 0x0E, 0x12, 0xE7,
0xBC, 0x30, 0x3E, 0xF7, 0x90, 0xFE, 0xD8, 0x74,
0x01, 0xF0, 0xD2, 0x09, 0x30, 0x09, 0x03, 0x7F,
0x00, 0x22, 0x12, 0xE7, 0xB0, 0x20, 0x36, 0x11,
0x20, 0x37, 0x0E, 0x12, 0xE7, 0xBC, 0x30, 0x3E,
0xF4, 0x90, 0xFE, 0xD8, 0x74, 0x01, 0xF0, 0xD2,
0x37, 0x30, 0x37, 0x03, 0x7F, 0x00, 0x22, 0x90,
0xFE, 0x64, 0x74, 0x10, 0x45, 0x4E, 0xF0, 0x90,
0xFE, 0x64, 0x74, 0x00, 0x45, 0x4E, 0xF0, 0x12,
0x2F, 0x65, 0x12, 0x2F, 0x68, 0xBF, 0x00, 0x09,
0x74, 0x02, 0x90, 0xEB, 0xC2, 0xF0, 0x7F, 0x00,
0x22, 0x12, 0x2F, 0x6B, 0xBF, 0x00, 0x0F, 0x12,
0x2F, 0x6E, 0xBF, 0x00, 0x09, 0x74, 0x01, 0x90,
0xEB, 0xC2, 0xF0, 0x7F, 0x00, 0x22, 0x30, 0x06,
0x0A, 0x90, 0xFF, 0x2A, 0x74, 0x02, 0xF0, 0xA3,
0x74, 0x00, 0xF0, 0x7F, 0x01, 0x22, 0x12, 0xE3,
0xAA, 0x74, 0x01, 0x90, 0xE9, 0xCB, 0xF0, 0xE5,
0x3E, 0xC3, 0x13, 0x90, 0xE9, 0xCA, 0xF0, 0xC0,
0xE0, 0x75, 0xF0, 0x02, 0xC0, 0xF0, 0x12, 0xE2,
0x2F, 0xEF, 0x70, 0x21, 0x20, 0x37, 0x07, 0x20,
0x09, 0x04, 0xD0, 0xF0, 0x80, 0x05, 0xD0, 0xF0,
0xD5, 0xF0, 0xE9, 0xD0, 0xE0, 0x90, 0xFF, 0x28,
0xE0, 0x30, 0xE7, 0xFC, 0x90, 0xFF, 0x28, 0xE0,
0x44, 0x01, 0xF0, 0xC3, 0x22, 0xD0, 0xF0, 0x90,
0xE9, 0xD2, 0xE0, 0x24, 0x01, 0xF0, 0x90, 0xE9,
0xD1, 0xE0, 0x34, 0x00, 0xF0, 0x90, 0xE9, 0xD0,
0xE0, 0x34, 0x00, 0xF0, 0xD0, 0xE0, 0x14, 0x70,
0xB6, 0x75, 0x3C, 0x00, 0x75, 0x3D, 0x00, 0x75,
0x3E, 0x00, 0x75, 0x3F, 0x00, 0xD3, 0x22, 0xC2,
0x08, 0xC2, 0x36, 0xC2, 0x37, 0x90, 0xFE, 0x68,
0x74, 0x31, 0xF0, 0x90, 0xE9, 0xD0, 0xE0, 0xF8,
0xA3, 0xE0, 0xF9, 0xA3, 0xE0, 0x90, 0xFE, 0x6B,
0xF0, 0xA3, 0xE9, 0xF0, 0xA3, 0xE8, 0xF0, 0x90,
0xFE, 0x6F, 0x74, 0x0F, 0xF0, 0x90, 0xFE, 0x70,
0xE0, 0x54, 0xFC, 0x44, 0x22, 0xF0, 0x90, 0xE9,
0xCB, 0xE0, 0x70, 0x0C, 0x90, 0xFE, 0xC0, 0x74,
0xF4, 0xF0, 0xA3, 0x74, 0x00, 0xF0, 0x80, 0x0A,
0x90, 0xFE, 0xC0, 0x74, 0xF0, 0xF0, 0xA3, 0x74,
0x00, 0xF0, 0x90, 0xFE, 0x64, 0x74, 0xF0, 0x45,
0x4E, 0xF0, 0x90, 0xFE, 0x64, 0x74, 0xB0, 0x45,
0x4E, 0xF0, 0x90, 0xFE, 0x6E, 0x74, 0x81, 0xF0,
0x90, 0xE9, 0xCB, 0xE0, 0x70, 0x0D, 0x90, 0xFE,
0xC6, 0x74, 0x02, 0xF0, 0xA3, 0x74, 0x0F, 0xF0,
0x02, 0xE3, 0x56, 0x20, 0x2D, 0x03, 0x02, 0xE2,
0xEF, 0x90, 0xFE, 0xC6, 0x74, 0x01, 0xF0, 0xA3,
0x74, 0xFF, 0xF0, 0x90, 0xFF, 0x09, 0x30, 0x0A,
0x04, 0xE0, 0x30, 0xE1, 0xF9, 0x90, 0xFE, 0xC4,
0x74, 0x23, 0xF0, 0x12, 0xE7, 0xB0, 0x20, 0x36,
0x11, 0x20, 0x37, 0x0E, 0x12, 0xE7, 0xBC, 0x30,
0x3E, 0xF4, 0x90, 0xFE, 0xD8, 0x74, 0x01, 0xF0,
0xD2, 0x37, 0x30, 0x37, 0x02, 0x61, 0xA7, 0x90,
0xFF, 0x09, 0xE0, 0x30, 0xE1, 0x06, 0x90, 0xFF,
0x23, 0x74, 0x80, 0xF0, 0x02, 0xE3, 0x3F, 0x90,
0xFE, 0xC6, 0xE4, 0xF0, 0xA3, 0x74, 0x3F, 0xF0,
0x78, 0x08, 0xC0, 0x00, 0xC2, 0x36, 0xC2, 0x37,
0x90, 0xFF, 0x09, 0x30, 0x0A, 0x04, 0xE0, 0x30,
0xE1, 0xF9, 0x90, 0xFE, 0xC4, 0x74, 0x23, 0xF0,
0x12, 0xE7, 0xB0, 0x20, 0x36, 0x11, 0x20, 0x37,
0x0E, 0x12, 0xE7, 0xBC, 0x30, 0x3E, 0xF4, 0x90,
0xFE, 0xD8, 0x74, 0x01, 0xF0, 0xD2, 0x37, 0x90,
0xFF, 0x09, 0xE0, 0x30, 0xE1, 0x06, 0x90, 0xFF,
0x23, 0x74, 0x80, 0xF0, 0x30, 0x37, 0x04, 0xD0,
0x00, 0x80, 0x6C, 0xD0, 0x00, 0xD8, 0xBB, 0xC2,
0x36, 0xC2, 0x37, 0x90, 0xFE, 0xC6, 0xE4, 0xF0,
0xA3, 0x74, 0x0F, 0xF0, 0x90, 0xFE, 0xC0, 0x74,
0xF6, 0xF0, 0xA3, 0x74, 0x00, 0xF0, 0x90, 0xFE,
0xC4, 0x74, 0x23, 0xF0, 0x12, 0xE7, 0xB0, 0x20,
0x36, 0x11, 0x20, 0x37, 0x0E, 0x12, 0xE7, 0xBC,
0x30, 0x3E, 0xF4, 0x90, 0xFE, 0xD8, 0x74, 0x01,
0xF0, 0xD2, 0x37, 0x30, 0x37, 0x02, 0x80, 0x2F,
0xC2, 0x09, 0x12, 0xE7, 0xB0, 0x20, 0x08, 0x0E,
0x12, 0xE7, 0xBC, 0x30, 0x3E, 0xF7, 0x90, 0xFE,
0xD8, 0x74, 0x01, 0xF0, 0xD2, 0x09, 0x30, 0x09,
0x02, 0x80, 0x14, 0x90, 0xFE, 0x64, 0x74, 0x90,
0x45, 0x4E, 0xF0, 0x90, 0xFE, 0x64, 0x74, 0x80,
0x45, 0x4E, 0xF0, 0x12, 0x2F, 0x59, 0x22, 0x7F,
0x00, 0x22, 0x90, 0xF6, 0x00, 0x7F, 0x06, 0x74,
0xFF, 0xF0, 0xA3, 0xDF, 0xFC, 0x7B, 0x02, 0x7C,
0xE9, 0x7D, 0xD3, 0x7E, 0xF6, 0x7F, 0x06, 0x12,
0x2F, 0x71, 0x7B, 0x02, 0x7C, 0xE9, 0x7D, 0xD3,
0x7E, 0xF6, 0x7F, 0x0B, 0x12, 0x2F, 0x71, 0x22,
0x90, 0xFE, 0xC6, 0xE4, 0xF0, 0xA3, 0x74, 0x0F,
0xF0, 0x90, 0xFE, 0x6F, 0xF0, 0x90, 0xFE, 0x70,
0xE0, 0x54, 0xFC, 0xF0, 0x90, 0xFE, 0xC0, 0x74,
0xF6, 0xF0, 0xA3, 0x74, 0x00, 0xF0, 0x90, 0xFE,
0x68, 0x74, 0x21, 0xF0, 0x90, 0xFE, 0x66, 0xE0,
0x54, 0xEF, 0xF0, 0x90, 0xE9, 0xD3, 0xE0, 0xF5,
0x08, 0xA3, 0xE0, 0xF5, 0x09, 0x90, 0xFF, 0x09,
0xE0, 0x30, 0xE5, 0xFC, 0xE4, 0xF5, 0x10, 0x7E,
0xF4, 0x7F, 0x00, 0xC0, 0x06, 0xC0, 0x07, 0xC2,
0x36, 0xC2, 0x37, 0xC2, 0x09, 0x90, 0xE9, 0xCD,
0xE0, 0xF8, 0xA3, 0xE0, 0xF9, 0xA3, 0xE0, 0x90,
0xFE, 0x6B, 0xF0, 0xA3, 0xE9, 0xF0, 0xA3, 0xE8,
0xF0, 0x90, 0xFE, 0x6E, 0x74, 0x71, 0xF0, 0x90,
0xFE, 0xC4, 0x74, 0x21, 0xF0, 0x90, 0xFE, 0x65,
0x12, 0xE7, 0xB0, 0xE0, 0x20, 0xE4, 0x11, 0x12,
0xE7, 0xBC, 0x30, 0x3E, 0xF6, 0x90, 0xFE, 0xD8,
0x74, 0x01, 0xF0, 0xD2, 0x09, 0x02, 0xE4, 0x72,
0x74, 0x10, 0xF0, 0x12, 0xE7, 0xB0, 0x20, 0x36,
0x11, 0x20, 0x37, 0x0E, 0x12, 0xE7, 0xBC, 0x30,
0x3E, 0xF4, 0x90, 0xFE, 0xD8, 0x74, 0x01, 0xF0,
0xD2, 0x37, 0x20, 0x09, 0x05, 0x20, 0x37, 0x02,
0x80, 0x10, 0x90, 0xFE, 0x66, 0xE0, 0x44, 0x10,
0xF0, 0x12, 0x2F, 0x5C, 0xD0, 0x07, 0xD0, 0x06,
0xC3, 0x22, 0xD0, 0x07, 0xD0, 0x06, 0x7B, 0x10,
0x7C, 0xF6, 0x7D, 0x00, 0x12, 0x2F, 0x71, 0x05,
0x10, 0xC3, 0xE5, 0x09, 0x94, 0x01, 0xF5, 0x09,
0xE5, 0x08, 0x94, 0x00, 0xF5, 0x08, 0x45, 0x09,
0x70, 0x03, 0x02, 0xE4, 0xEF, 0x90, 0xE9, 0xCF,
0xE0, 0x24, 0x20, 0xF0, 0x90, 0xE9, 0xCE, 0xE0,
0x34, 0x00, 0xF0, 0x90, 0xE9, 0xCD, 0xE0, 0x34,
0x00, 0xF0, 0xC3, 0xEF, 0x24, 0x10, 0xFF, 0xEE,
0x34, 0x00, 0xFE, 0xE5, 0x10, 0x64, 0x20, 0x60,
0x03, 0x02, 0xE4, 0x13, 0x90, 0xFF, 0x2A, 0x74,
0x02, 0xF0, 0xA3, 0x74, 0x00, 0xF0, 0x75, 0x10,
0x00, 0x7E, 0xF4, 0x7F, 0x00, 0x90, 0xFF, 0x09,
0xE0, 0x30, 0xE5, 0xFC, 0x02, 0xE4, 0x13, 0xE5,
0x10, 0x60, 0x17, 0x7E, 0x00, 0x7F, 0x00, 0x78,
0x04, 0xC3, 0x33, 0xFF, 0xEE, 0x33, 0xFE, 0xEF,
0xD8, 0xF8, 0x90, 0xFF, 0x2A, 0xEE, 0xF0, 0xA3,
0xEF, 0xF0, 0x90, 0xFE, 0x66, 0xE0, 0x44, 0x10,
0xF0, 0x12, 0x2F, 0x5C, 0x78, 0x00, 0x88, 0x3C,
0x88, 0x3D, 0x88, 0x3E, 0x88, 0x3F, 0xD3, 0x22,
0x12, 0x2F, 0x5F, 0x12, 0x2F, 0x62, 0x90, 0xFE,
0xC6, 0xE4, 0xF0, 0xA3, 0x74, 0x0F, 0xF0, 0x90,
0xFE, 0x6F, 0xF0, 0x90, 0xFE, 0x70, 0xE0, 0x54,
0xFC, 0xF0, 0x90, 0xFE, 0xC0, 0x74, 0xF6, 0xF0,
0xA3, 0x74, 0x00, 0xF0, 0x90, 0xFE, 0x68, 0x74,
0x31, 0xF0, 0x90, 0xE9, 0xD3, 0xE0, 0xF8, 0xC0,
0x00, 0xC2, 0x08, 0xC2, 0x36, 0xC2, 0x37, 0x90,
0xE9, 0xD0, 0xE0, 0xF8, 0xA3, 0xE0, 0xF9, 0xA3,
0xE0, 0x90, 0xFE, 0x6B, 0xF0, 0xA3, 0xE9, 0xF0,
0xA3, 0xE8, 0xF0, 0x90, 0xFE, 0x6E, 0x74, 0x81,
0xF0, 0x90, 0xFE, 0xC4, 0x74, 0x23, 0xF0, 0x12,
0xE7, 0xB0, 0x20, 0x36, 0x11, 0x20, 0x37, 0x0E,
0x12, 0xE7, 0xBC, 0x30, 0x3E, 0xF4, 0x90, 0xFE,
0xD8, 0x74, 0x01, 0xF0, 0xD2, 0x37, 0x30, 0x37,
0x07, 0xD0, 0x00, 0x12, 0x2F, 0x5C, 0xC3, 0x22,
0xC2, 0x09, 0x12, 0xE7, 0xB0, 0x20, 0x08, 0x0E,
0x12, 0xE7, 0xBC, 0x30, 0x3E, 0xF7, 0x90, 0xFE,
0xD8, 0x74, 0x01, 0xF0, 0xD2, 0x09, 0x20, 0x09,
0xE0, 0x90, 0xE9, 0xD2, 0xE0, 0x24, 0x01, 0xF0,
0x90, 0xE9, 0xD1, 0xE0, 0x34, 0x00, 0xF0, 0x90,
0xE9, 0xD0, 0xE0, 0x34, 0x00, 0xF0, 0xD0, 0x00,
0x18, 0xE8, 0x60, 0x03, 0x02, 0xE5, 0x4F, 0x12,
0x2F, 0x5C, 0x75, 0x3C, 0x00, 0x75, 0x3D, 0x00,
0x75, 0x3E, 0x00, 0x75, 0x3F, 0x00, 0xD3, 0x22,
0x90, 0xE9, 0xD0, 0xE0, 0xF8, 0xA3, 0xE0, 0xF9,
0xA3, 0xE0, 0x90, 0xFE, 0x6B, 0xF0, 0xA3, 0xE9,
0xF0, 0xA3, 0xE8, 0xF0, 0x90, 0xFE, 0x68, 0x74,
0x00, 0xF0, 0xC2, 0x08, 0x90, 0xFE, 0x6E, 0x74,
0xB1, 0xF0, 0xC2, 0x09, 0x12, 0xE7, 0xB0, 0x20,
0x08, 0x0E, 0x12, 0xE7, 0xBC, 0x30, 0x3E, 0xF7,
0x90, 0xFE, 0xD8, 0x74, 0x01, 0xF0, 0xD2, 0x09,
0x20, 0x09, 0x1E, 0x90, 0xFE, 0x70, 0xE0, 0x44,
0x10, 0xF0, 0x54, 0xEF, 0xF0, 0x12, 0x2F, 0x59,
0xEF, 0x60, 0x0E, 0x75, 0x3C, 0x00, 0x75, 0x3D,
0x00, 0x75, 0x3E, 0x00, 0x75, 0x3F, 0x00, 0xD3,
0x22, 0xC3, 0x22, 0x7B, 0x03, 0x7C, 0xE9, 0x7D,
0xCD, 0x7E, 0xE9, 0x7F, 0xD7, 0x12, 0x2F, 0x71,
0x12, 0xE3, 0xAA, 0x90, 0xE9, 0xD5, 0xE0, 0x60,
0x12, 0xF9, 0x12, 0xE7, 0x17, 0x40, 0x01, 0x22,
0x90, 0xF6, 0x00, 0x78, 0x06, 0x74, 0xFF, 0xF0,
0xA3, 0xD8, 0xFC, 0x74, 0x01, 0x90, 0xE9, 0xCB,
0xF0, 0xE5, 0x3E, 0xC3, 0x13, 0x90, 0xE9, 0xCA,
0xF0, 0xC0, 0xE0, 0x75, 0xF0, 0x02, 0xC0, 0xF0,
0x12, 0xE2, 0x2F, 0xEF, 0x70, 0x21, 0x20, 0x37,
0x07, 0x20, 0x09, 0x04, 0xD0, 0xF0, 0x80, 0x05,
0xD0, 0xF0, 0xD5, 0xF0, 0xE9, 0xD0, 0xE0, 0x90,
0xFF, 0x28, 0xE0, 0x30, 0xE7, 0xFC, 0x90, 0xFF,
0x28, 0xE0, 0x44, 0x01, 0xF0, 0xC3, 0x22, 0xD0,
0xF0, 0x90, 0xE9, 0xD2, 0xE0, 0x24, 0x01, 0xF0,
0x90, 0xE9, 0xD1, 0xE0, 0x34, 0x00, 0xF0, 0x90,
0xE9, 0xD0, 0xE0, 0x34, 0x00, 0xF0, 0xD0, 0xE0,
0x14, 0x70, 0xB6, 0x90, 0xE9, 0xD5, 0xE0, 0xF8,
0x90, 0xE9, 0xCA, 0xE0, 0x28, 0xF5, 0xF0, 0xC3,
0x74, 0x20, 0x95, 0xF0, 0x60, 0x22, 0xF9, 0x90,
0xE9, 0xCA, 0xE0, 0xF5, 0xF0, 0x90, 0xE9, 0xCF,
0xE0, 0x25, 0xF0, 0xF0, 0x90, 0xE9, 0xCE, 0xE0,
0x34, 0x00, 0xF0, 0x90, 0xE9, 0xCD, 0xE0, 0x34,
0x00, 0xF0, 0x12, 0xE7, 0x17, 0x40, 0x01, 0x22,
0x90, 0xE9, 0xD6, 0xE0, 0x70, 0x13, 0x7B, 0x03,
0x7C, 0xE9, 0x7D, 0xD7, 0x7E, 0xE9, 0x7F, 0xD0,
0x12, 0x2F, 0x71, 0x12, 0xE5, 0xE0, 0x40, 0x01,
0x22, 0x75, 0x3C, 0x00, 0x75, 0x3D, 0x00, 0x75,
0x3E, 0x00, 0x75, 0x3F, 0x00, 0xD3, 0x22, 0x90,
0xE9, 0xD6, 0xE0, 0x60, 0x18, 0x74, 0xFF, 0x90,
0xF4, 0x00, 0x78, 0xFF, 0xF0, 0xA3, 0x18, 0xB8,
0x00, 0xFA, 0x78, 0xFF, 0xF0, 0xA3, 0x18, 0xB8,
0x00, 0xFA, 0xF0, 0xA3, 0xF0, 0xC0, 0x01, 0x12,
0xE7, 0x70, 0x40, 0x04, 0xD0, 0x01, 0xC3, 0x22,
0x90, 0xE9, 0xCF, 0xE0, 0x24, 0x01, 0xF0, 0x90,
0xE9, 0xCE, 0xE0, 0x34, 0x00, 0xF0, 0x90, 0xE9,
0xCD, 0xE0, 0x34, 0x00, 0xF0, 0x90, 0xE9, 0xD2,
0xE0, 0x24, 0x01, 0xF0, 0x90, 0xE9, 0xD1, 0xE0,
0x34, 0x00, 0xF0, 0x90, 0xE9, 0xD0, 0xE0, 0x34,
0x00, 0xF0, 0xD0, 0x01, 0xD9, 0xC7, 0xD3, 0x22,
0xC2, 0x06, 0x90, 0xE9, 0xD6, 0xE0, 0x70, 0x28,
0x12, 0xE0, 0xD8, 0xEF, 0x60, 0x03, 0x02, 0xE7,
0xA0, 0x90, 0xEB, 0xC2, 0xE0, 0x60, 0x17, 0x64,
0x02, 0x60, 0x15, 0x90, 0xF6, 0x00, 0x78, 0x06,
0x74, 0xFF, 0xF0, 0xA3, 0xD8, 0xFC, 0x74, 0xF0,
0x90, 0xF6, 0x04, 0xF0, 0x80, 0x02, 0xC3, 0x22,
0xE4, 0x90, 0xE9, 0xCB, 0xF0, 0x12, 0xE2, 0x2F,
0xEF, 0x70, 0x03, 0x02, 0xE7, 0x81, 0xD3, 0x22,
0xC2, 0x3E, 0x75, 0x7C, 0x00, 0x75, 0x7D, 0x00,
0x75, 0x7E, 0x00, 0x22, 0x05, 0x7C, 0xE5, 0x7C,
0x70, 0x14, 0x05, 0x7D, 0xE5, 0x7D, 0x70, 0x04,
0x05, 0x7E, 0x80, 0x0A, 0xB4, 0x17, 0x07, 0xE5,
0x7E, 0xB4, 0x06, 0x02, 0xD2, 0x3E, 0x22, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x58, 0x44, 0x2D, 0x52, 0x57, 0x20, 0x20, 0x20,
0x20, 0x20, 0x20, 0x31, 0x30, 0x30, 0x30, 0x30 };

