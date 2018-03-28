#ifndef VBE_H
#define VBE_H

#include <core/system.h>

// The official VBE Information Block
struct vbe_info_block
{
   union        {
       uint8_t      sig_chr[4];
       uint32_t     sig32;
   }            vbe_signature;
   uint16_t     vbe_version;
   uint32_t     oem_string;
   uint8_t      capabilities[4];
   uint16_t     video_mode_ptr_off;
   uint16_t     video_mode_ptr_seg;
   uint16_t     total_memory;
   uint16_t     oem_software_rev;
   uint32_t     oem_vendor_name;
   uint32_t     oem_product_name;
   uint32_t     oem_product_rev;
   uint16_t     reserved[111]; // used for dynamically generated mode list
   uint8_t      oemdata[256];
} __packed;

struct mode_info_block
{
// Mandatory information for all VBE revisions
   uint16_t mode_attributes;
   uint8_t  win_a_attributes;
   uint8_t  win_b_attributes;
   uint16_t win_granularity;
   uint16_t win_size;
   uint16_t win_a_segment;
   uint16_t win_b_segment;
   uint32_t win_func_ptr;
   uint16_t bytes_per_scanline;
// Mandatory information for VBE 1.2 and above
   uint16_t x_resolution;
   uint16_t y_resolution;
   uint8_t  x_char_size;
   uint8_t  y_char_size;
   uint8_t  number_of_planes;
   uint8_t  bits_per_pixel;
   uint8_t  number_of_banks;
   uint8_t  memory_model;
   uint8_t  bank_size;
   uint8_t  number_of_image_pages;
   uint8_t  reserved_page;
// Direct Color fields (required for direct/6 and YUV/7 memory models)
   uint8_t  red_mask_size;
   uint8_t  red_field_position;
   uint8_t  green_mask_size;
   uint8_t  green_field_position;
   uint8_t  blue_mask_size;
   uint8_t  blue_field_position;
   uint8_t  rsvd_mask_size;
   uint8_t  rsvd_field_position;
   uint8_t  direct_color_mode_info;
// Mandatory information for VBE 2.0 and above
   uint32_t phys_base_ptr;
   uint32_t off_screen_mem_offset;
   uint16_t off_screen_mem_size;
// Mandatory information for VBE 3.0 and above
   uint16_t lin_bytes_per_scanline;
   uint8_t  bnk_number_of_pages;
   uint8_t  lin_number_of_pages;
   uint8_t  lin_red_mask_size;
   uint8_t  lin_red_field_position;
   uint8_t  lin_green_mask_size;
   uint8_t  lin_green_field_position;
   uint8_t  lin_blue_mask_size;
   uint8_t  lin_blue_field_position;
   uint8_t  lin_rsvd_mask_size;
   uint8_t  lin_rsvd_field_position;
   uint32_t max_pixel_clock;
   uint8_t  reserved[189];
} __packed;

// VBE Return Status Info
// AL
#define VBE_RETURN_STATUS_SUPPORTED                      0x4F
#define VBE_RETURN_STATUS_UNSUPPORTED                    0x00
// AH
#define VBE_RETURN_STATUS_SUCCESSFULL                    0x00
#define VBE_RETURN_STATUS_FAILED                         0x01
#define VBE_RETURN_STATUS_NOT_SUPPORTED                  0x02
#define VBE_RETURN_STATUS_INVALID                        0x03

// VBE Mode Numbers

#define VBE_MODE_VESA_DEFINED                            0x0100
#define VBE_MODE_REFRESH_RATE_USE_CRTC                   0x0800
#define VBE_MODE_LINEAR_FRAME_BUFFER                     0x4000
#define VBE_MODE_PRESERVE_DISPLAY_MEMORY                 0x8000

// VBE GFX Mode Number

#define VBE_VESA_MODE_640X400X8                          0x100
#define VBE_VESA_MODE_640X480X8                          0x101
#define VBE_VESA_MODE_800X600X4                          0x102
#define VBE_VESA_MODE_800X600X8                          0x103
#define VBE_VESA_MODE_1024X768X4                         0x104
#define VBE_VESA_MODE_1024X768X8                         0x105
#define VBE_VESA_MODE_1280X1024X4                        0x106
#define VBE_VESA_MODE_1280X1024X8                        0x107
#define VBE_VESA_MODE_320X200X1555                       0x10D
#define VBE_VESA_MODE_320X200X565                        0x10E
#define VBE_VESA_MODE_320X200X888                        0x10F
#define VBE_VESA_MODE_640X480X1555                       0x110
#define VBE_VESA_MODE_640X480X565                        0x111
#define VBE_VESA_MODE_640X480X888                        0x112
#define VBE_VESA_MODE_800X600X1555                       0x113
#define VBE_VESA_MODE_800X600X565                        0x114
#define VBE_VESA_MODE_800X600X888                        0x115
#define VBE_VESA_MODE_1024X768X1555                      0x116
#define VBE_VESA_MODE_1024X768X565                       0x117
#define VBE_VESA_MODE_1024X768X888                       0x118
#define VBE_VESA_MODE_1280X1024X1555                     0x119
#define VBE_VESA_MODE_1280X1024X565                      0x11A
#define VBE_VESA_MODE_1280X1024X888                      0x11B
#define VBE_VESA_MODE_1600X1200X8                        0x11C
#define VBE_VESA_MODE_1600X1200X1555                     0x11D
#define VBE_VESA_MODE_1600X1200X565                      0x11E
#define VBE_VESA_MODE_1600X1200X888                      0x11F

// BOCHS/PLEX86 'own' mode numbers
#define VBE_OWN_MODE_320X200X8888                        0x140
#define VBE_OWN_MODE_640X400X8888                        0x141
#define VBE_OWN_MODE_640X480X8888                        0x142
#define VBE_OWN_MODE_800X600X8888                        0x143
#define VBE_OWN_MODE_1024X768X8888                       0x144
#define VBE_OWN_MODE_1280X1024X8888                      0x145
#define VBE_OWN_MODE_320X200X8                           0x146
#define VBE_OWN_MODE_1600X1200X8888                      0x147
#define VBE_OWN_MODE_1152X864X8                          0x148
#define VBE_OWN_MODE_1152X864X1555                       0x149
#define VBE_OWN_MODE_1152X864X565                        0x14a
#define VBE_OWN_MODE_1152X864X888                        0x14b
#define VBE_OWN_MODE_1152X864X8888                       0x14c

#define VBE_VESA_MODE_END_OF_LIST                        0xFFFF

// Capabilities

#define VBE_CAPABILITY_8BIT_DAC                          0x0001
#define VBE_CAPABILITY_NOT_VGA_COMPATIBLE                0x0002
#define VBE_CAPABILITY_RAMDAC_USE_BLANK_BIT              0x0004
#define VBE_CAPABILITY_STEREOSCOPIC_SUPPORT              0x0008
#define VBE_CAPABILITY_STEREO_VIA_VESA_EVC               0x0010

// Mode Attributes

#define VBE_MODE_ATTRIBUTE_SUPPORTED                     0x0001
#define VBE_MODE_ATTRIBUTE_EXTENDED_INFORMATION_AVAILABLE  0x0002
#define VBE_MODE_ATTRIBUTE_TTY_BIOS_SUPPORT              0x0004
#define VBE_MODE_ATTRIBUTE_COLOR_MODE                    0x0008
#define VBE_MODE_ATTRIBUTE_GRAPHICS_MODE                 0x0010
#define VBE_MODE_ATTRIBUTE_NOT_VGA_COMPATIBLE            0x0020
#define VBE_MODE_ATTRIBUTE_NO_VGA_COMPATIBLE_WINDOW      0x0040
#define VBE_MODE_ATTRIBUTE_LINEAR_FRAME_BUFFER_MODE      0x0080
#define VBE_MODE_ATTRIBUTE_DOUBLE_SCAN_MODE              0x0100
#define VBE_MODE_ATTRIBUTE_INTERLACE_MODE                0x0200
#define VBE_MODE_ATTRIBUTE_HARDWARE_TRIPLE_BUFFER        0x0400
#define VBE_MODE_ATTRIBUTE_HARDWARE_STEREOSCOPIC_DISPLAY 0x0800
#define VBE_MODE_ATTRIBUTE_DUAL_DISPLAY_START_ADDRESS    0x1000

#define VBE_MODE_ATTTRIBUTE_LFB_ONLY                     ( VBE_MODE_ATTRIBUTE_NO_VGA_COMPATIBLE_WINDOW | VBE_MODE_ATTRIBUTE_LINEAR_FRAME_BUFFER_MODE )

// Window attributes

#define VBE_WINDOW_ATTRIBUTE_RELOCATABLE                 0x01
#define VBE_WINDOW_ATTRIBUTE_READABLE                    0x02
#define VBE_WINDOW_ATTRIBUTE_WRITEABLE                   0x04

// Memory model

#define VBE_MEMORYMODEL_TEXT_MODE                        0x00
#define VBE_MEMORYMODEL_CGA_GRAPHICS                     0x01
#define VBE_MEMORYMODEL_HERCULES_GRAPHICS                0x02
#define VBE_MEMORYMODEL_PLANAR                           0x03
#define VBE_MEMORYMODEL_PACKED_PIXEL                     0x04
#define VBE_MEMORYMODEL_NON_CHAIN_4_256                  0x05
#define VBE_MEMORYMODEL_DIRECT_COLOR                     0x06
#define VBE_MEMORYMODEL_YUV                              0x07

// DirectColorModeInfo

#define VBE_DIRECTCOLOR_COLOR_RAMP_PROGRAMMABLE          0x01
#define VBE_DIRECTCOLOR_RESERVED_BITS_AVAILABLE          0x02

struct pm_info_block {
    char        signature[4];
    uint16_t    entry_point;
    uint16_t    pm_initalize;
    uint16_t    bios_data_sel;
    uint16_t    a000h;
    uint16_t    b000h;
    uint16_t    b800h;
    uint16_t    code_seg_sel;
    uint8_t     in_protect_mode;
    uint8_t     checksum;
} __packed;

#endif
