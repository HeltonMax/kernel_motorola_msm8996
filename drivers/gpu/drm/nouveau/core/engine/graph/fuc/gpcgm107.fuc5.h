uint32_t gm107_grgpc_data[] = {
/* 0x0000: gpc_mmio_list_head */
	0x0000006c,
/* 0x0004: gpc_mmio_list_tail */
/* 0x0004: tpc_mmio_list_head */
	0x0000006c,
/* 0x0008: tpc_mmio_list_tail */
/* 0x0008: unk_mmio_list_head */
	0x0000006c,
/* 0x000c: unk_mmio_list_tail */
	0x0000006c,
/* 0x0010: gpc_id */
	0x00000000,
/* 0x0014: tpc_count */
	0x00000000,
/* 0x0018: tpc_mask */
	0x00000000,
/* 0x001c: unk_count */
	0x00000000,
/* 0x0020: unk_mask */
	0x00000000,
/* 0x0024: cmd_queue */
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
};

uint32_t gm107_grgpc_code[] = {
	0x03140ef5,
/* 0x0004: queue_put */
	0x9800d898,
	0x86f001d9,
	0xf489a408,
	0x020f0b1b,
	0x0002f87e,
/* 0x001a: queue_put_next */
	0x98c400f8,
	0x0384b607,
	0xb6008dbb,
	0x8eb50880,
	0x018fb500,
	0xf00190b6,
	0xd9b50f94,
/* 0x0037: queue_get */
	0xf400f801,
	0xd8980131,
	0x01d99800,
	0x0bf489a4,
	0x0789c421,
	0xbb0394b6,
	0x90b6009d,
	0x009e9808,
	0xb6019f98,
	0x84f00180,
	0x00d8b50f,
/* 0x0063: queue_get_done */
	0xf80132f4,
/* 0x0065: nv_rd32 */
	0xf0ecb200,
	0x00801fc9,
	0x0cf601ca,
/* 0x0073: nv_rd32_wait */
	0x8c04bd00,
	0xcf01ca00,
	0xccc800cc,
	0xf61bf41f,
	0xec7e060a,
	0x008f0000,
	0xffcf01cb,
/* 0x008f: nv_wr32 */
	0x8000f800,
	0xf601cc00,
	0x04bd000f,
	0xc9f0ecb2,
	0x1ec9f01f,
	0x01ca0080,
	0xbd000cf6,
/* 0x00a9: nv_wr32_wait */
	0xca008c04,
	0x00cccf01,
	0xf41fccc8,
	0x00f8f61b,
/* 0x00b8: wait_donez */
	0x99f094bd,
	0x37008000,
	0x0009f602,
	0x008004bd,
	0x0af60206,
/* 0x00cf: wait_donez_ne */
	0x8804bd00,
	0xcf010000,
	0x8aff0088,
	0xf61bf488,
	0x99f094bd,
	0x17008000,
	0x0009f602,
	0x00f804bd,
/* 0x00ec: wait_doneo */
	0x99f094bd,
	0x37008000,
	0x0009f602,
	0x008004bd,
	0x0af60206,
/* 0x0103: wait_doneo_e */
	0x8804bd00,
	0xcf010000,
	0x8aff0088,
	0xf60bf488,
	0x99f094bd,
	0x17008000,
	0x0009f602,
	0x00f804bd,
/* 0x0120: mmctx_size */
/* 0x0122: nv_mmctx_size_loop */
	0xe89894bd,
	0x1a85b600,
	0xb60180b6,
	0x98bb0284,
	0x04e0b600,
	0x1bf4efa4,
	0xf89fb2ec,
/* 0x013d: mmctx_xfer */
	0xf094bd00,
	0x00800199,
	0x09f60237,
	0xbd04bd00,
	0x05bbfd94,
	0x800f0bf4,
	0xf601c400,
	0x04bd000b,
/* 0x015f: mmctx_base_disabled */
	0xfd0099f0,
	0x0bf405ee,
	0xc6008018,
	0x000ef601,
	0x008004bd,
	0x0ff601c7,
	0xf004bd00,
/* 0x017a: mmctx_multi_disabled */
	0xabc80199,
	0x10b4b600,
	0xc80cb9f0,
	0xe4b601ae,
	0x05befd11,
	0x01c50080,
	0xbd000bf6,
/* 0x0195: mmctx_exec_loop */
/* 0x0195: mmctx_wait_free */
	0xc5008e04,
	0x00eecf01,
	0xf41fe4f0,
	0xce98f60b,
	0x05e9fd00,
	0x01c80080,
	0xbd000ef6,
	0x04c0b604,
	0x1bf4cda4,
	0x02abc8df,
/* 0x01bf: mmctx_fini_wait */
	0x8b1c1bf4,
	0xcf01c500,
	0xb4f000bb,
	0x10b4b01f,
	0x0af31bf4,
	0x00b87e05,
	0x250ef400,
/* 0x01d8: mmctx_stop */
	0xb600abc8,
	0xb9f010b4,
	0x12b9f00c,
	0x01c50080,
	0xbd000bf6,
/* 0x01ed: mmctx_stop_wait */
	0xc5008b04,
	0x00bbcf01,
	0xf412bbc8,
/* 0x01fa: mmctx_done */
	0x94bdf61b,
	0x800199f0,
	0xf6021700,
	0x04bd0009,
/* 0x020a: strand_wait */
	0xa0f900f8,
	0xb87e020a,
	0xa0fc0000,
/* 0x0216: strand_pre */
	0x0c0900f8,
	0x024afc80,
	0xbd0009f6,
	0x020a7e04,
/* 0x0227: strand_post */
	0x0900f800,
	0x4afc800d,
	0x0009f602,
	0x0a7e04bd,
	0x00f80002,
/* 0x0238: strand_set */
	0xfc800f0c,
	0x0cf6024f,
	0x0c04bd00,
	0x4afc800b,
	0x000cf602,
	0xfc8004bd,
	0x0ef6024f,
	0x0c04bd00,
	0x4afc800a,
	0x000cf602,
	0x0a7e04bd,
	0x00f80002,
/* 0x0268: strand_ctx_init */
	0x99f094bd,
	0x37008003,
	0x0009f602,
	0x167e04bd,
	0x030e0002,
	0x0002387e,
	0xfc80c4bd,
	0x0cf60247,
	0x0c04bd00,
	0x4afc8001,
	0x000cf602,
	0x0a7e04bd,
	0x0c920002,
	0x46fc8001,
	0x000cf602,
	0x020c04bd,
	0x024afc80,
	0xbd000cf6,
	0x020a7e04,
	0x02277e00,
	0x42008800,
	0x20008902,
	0x0099cf02,
/* 0x02c7: ctx_init_strand_loop */
	0xf608fe95,
	0x8ef6008e,
	0x808acf40,
	0xb606a5b6,
	0xeabb01a0,
	0x0480b600,
	0xf40192b6,
	0xe4b6e81b,
	0xf2efbc08,
	0x99f094bd,
	0x17008003,
	0x0009f602,
	0x00f804bd,
/* 0x02f8: error */
	0xffb2e0f9,
	0x4098148e,
	0x00008f7e,
	0xffb2010f,
	0x409c1c8e,
	0x00008f7e,
	0x00f8e0fc,
/* 0x0314: init */
	0x004104bd,
	0x0011cf42,
	0x010911e7,
	0xfe0814b6,
	0x02020014,
	0xf6120040,
	0x04bd0002,
	0xfe047241,
	0x00400010,
	0x0000f607,
	0x040204bd,
	0xf6040040,
	0x04bd0002,
	0x821031f4,
	0xcf018200,
	0x01030022,
	0xbb1f24f0,
	0x32b60432,
	0x0502b501,
	0x820603b5,
	0xcf018600,
	0x02b50022,
	0x0c308e04,
	0xbd24bd50,
/* 0x0377: init_unk_loop */
	0x7e44bd34,
	0xb0000065,
	0x0bf400f6,
	0xbb010f0e,
	0x4ffd04f2,
	0x0130b605,
/* 0x038c: init_unk_next */
	0xb60120b6,
	0x26b004e0,
	0xe21bf402,
/* 0x0398: init_unk_done */
	0xb50703b5,
	0x00820804,
	0x22cf0201,
	0x9534bd00,
	0x00800825,
	0x05f601c0,
	0x8004bd00,
	0xf601c100,
	0x04bd0005,
	0x98000e98,
	0x207e010f,
	0x2fbb0001,
	0x003fbb00,
	0x98010e98,
	0x207e020f,
	0x0e980001,
	0x00effd05,
	0xbb002ebb,
	0x0e98003e,
	0x030f9802,
	0x0001207e,
	0xfd070e98,
	0x2ebb00ef,
	0x003ebb00,
	0x800235b6,
	0xf601d300,
	0x04bd0003,
	0xb60825b6,
	0x20b60635,
	0x0130b601,
	0xb60824b6,
	0x2fb20834,
	0x0002687e,
	0x80003fbb,
	0xf6020100,
	0x04bd0003,
	0x29f024bd,
	0x3000801f,
	0x0002f602,
/* 0x0436: main */
	0x31f404bd,
	0x0028f400,
	0x377e240d,
	0x01f40000,
	0x04e4b0f4,
	0xfe1d18f4,
	0x06020181,
	0x12fd20bd,
	0x01e4b604,
	0xfe051efd,
	0x097e0018,
	0x0ef40005,
/* 0x0465: main_not_ctx_xfer */
	0x10ef94d4,
	0x7e01f5f0,
	0xf40002f8,
/* 0x0472: ih */
	0x80f9c70e,
	0xf90188fe,
	0xf990f980,
	0xf9b0f9a0,
	0xf9e0f9d0,
	0x4a04bdf0,
	0xaacf0200,
	0x04abc400,
	0x0d1f0bf4,
	0x1a004e24,
	0x4f00eecf,
	0xffcf1900,
	0x00047e00,
	0x40010e00,
	0x0ef61d00,
/* 0x04af: ih_no_fifo */
	0x4004bd00,
	0x0af60100,
	0xfc04bd00,
	0xfce0fcf0,
	0xfcb0fcd0,
	0xfc90fca0,
	0x0088fe80,
	0x32f480fc,
/* 0x04cf: hub_barrier_done */
	0x0f01f800,
	0x040e9801,
	0xb204febb,
	0x94188eff,
	0x008f7e40,
/* 0x04e3: ctx_redswitch */
	0x0f00f800,
	0x85008020,
	0x000ff601,
	0x080e04bd,
/* 0x04f0: ctx_redswitch_delay */
	0xf401e2b6,
	0xf5f1fd1b,
	0xf5f10800,
	0x00800200,
	0x0ff60185,
	0xf804bd00,
/* 0x0509: ctx_xfer */
	0x81008000,
	0x000ff602,
	0x11f404bd,
	0x04e37e07,
/* 0x0519: ctx_xfer_not_load */
	0x02167e00,
	0x8024bd00,
	0xf60247fc,
	0x04bd0002,
	0xb6012cf0,
	0xfc800320,
	0x02f6024a,
	0xf004bd00,
	0xa5f001ac,
	0x00008b02,
	0x040c9850,
	0xbb0fc4b6,
	0x0c9800bc,
	0x010d9800,
	0x3d7e000e,
	0xacf00001,
	0x40008b01,
	0x040c9850,
	0xbb0fc4b6,
	0x0c9800bc,
	0x020d9801,
	0x4e060f98,
	0x3d7e0800,
	0xacf00001,
	0x04a5f001,
	0x5030008b,
	0xb6040c98,
	0xbcbb0fc4,
	0x020c9800,
	0x98030d98,
	0x004e080f,
	0x013d7e02,
	0x020a7e00,
	0x0601f400,
/* 0x05a3: ctx_xfer_post */
	0x7e0712f4,
/* 0x05a7: ctx_xfer_done */
	0x7e000227,
	0xf80004cf,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
	0x00000000,
};
