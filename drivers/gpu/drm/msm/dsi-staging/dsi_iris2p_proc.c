#include "dsi_iris2p_def.h"

static struct demo_win_info iris_demo_win_info;
int iris_debug_power_opt_disable = 1;
int iris_debug_fw_download_disable = 0;
static u32 iris_color_x_buf[]= {
    4127,
    4072,
    4018,
    3967,
    3917,
    3869,
    3823,
    3779,
    3737,
    3697,
    3658,
    3621,
    3585,
    3551,
    3519,
    3487,
    3457,
    3429,
    3401,
    3375,
    3349,
    3325,
    3302,
    3279,
    3258,
    3237,
    3217,
    3198,
    3179,
    3161,
    3144,
    3128,
    3112,
    3097,
    3082,
    3067,
    3054,
    3040,
    3027,
    3015,
    3003,
    2991,
    2980,
    2969,
    2958,
    2948,
    2938,
    2928,
    2919,
    2910,
    2901,
    2892,
    2884,
    2876,
    2868,
    2860,
    2853,
    2845,
    2838,
    2831,
    2825,
    2818,
    2812,
    2806,
    2800,
    2794,
    2788,
    2782,
    2777,
    2772,
    2766,
    2761,
    2756,
    2751,
    2747,
    2742,
    2737,
    2733,
    2729,
    2724,
    2720,
    2716,
    2712,
    2708,
    2705,
    2701,
    2697,
    2693,
    2690,
    2687,
    2683,
    2680,
    2677,
    2673,
    2670,
    2667,
    2664,
    2661,
    2658,
    2655,
    2653,
    2650,
    2647,
    2645,
    2642,
    2639,
    2637,
    2634,
    2632,
    2630,
    2627,
    2625,
    2623,
};

static char iris_cmlut_burst_grcp_header[16]= {
	PWIL_TAG('P', 'W', 'I', 'L'),
	PWIL_TAG('G', 'R', 'C', 'P'),
	PWIL_U32(0x00000039),
	0x03, 0x00, PWIL_U16(0x37),
};
static u32 iris_reg_addr = IRIS_PWIL_ADDR;
bool iris_cm_setting_force_update = false;

static int iris_oprt_rotation_set(void __user *argp)
{
	int ret;
	bool rotationen;
	u32 top_ctrl0 = 0;
	u32 value;

	ret = copy_from_user(&value, argp, sizeof(u32));
	if (ret) {
		pr_err("iris2 can not copy form user %s\n", __func__);
		return ret;
	}
	rotationen = !!(value);
	pr_debug("iris2 rotationen = %d\n", rotationen);

	top_ctrl0 = (rotationen << ROTATION) | (0 << TRUE_CUT_EXT_EN)
				| (0 << INTERVAL_SHIFT_EN) | (1 << PHASE_SHIFT_EN);
	//todo
	//mutex_lock(&mfd->iris_conf.cmd_mutex);
	iris_cmd_reg_add(&meta_cmd, IRIS_MVC_ADDR + IRIS_MVC_TOP_CTRL0_OFF, top_ctrl0);
	iris_cmd_reg_add(&meta_cmd, IRIS_MVC_ADDR + IRIS_MVC_SW_UPDATE_OFF, 1);
	//mutex_unlock(&mfd->iris_conf.cmd_mutex);

	return ret;
}

static int memc_input_fps = 0;

static int iris_oprt_video_framerate_set(void __user *argp)
{
    int ret = 0;
    u32 new_framerate_ms;

    ret = copy_from_user(&new_framerate_ms, argp, sizeof(u32));
    if (ret) {
        pr_err("iris2 copy from user error\n");
        return -EINVAL;
    }
    pr_err("iris2 new fpkms = %u\n", new_framerate_ms);

    memc_input_fps = (new_framerate_ms + 500)/1000;

    return ret;
}

void iris_frc_configs_update(void)
{
    struct iris_frc_setting *frc_setting = &iris_info.frc_setting;
    struct iris_mode_state *mode_state = &iris_info.mode_state;

    if (mode_state->sf_notify_mode != IRIS_MODE_FRC)
        return;

    if (memc_input_fps == frc_setting->in_fps)
        return;

    iris_video_fps_get(memc_input_fps);

    iris_update_frc_configs();
}

void iris_video_fps_get(u32 data)
{
	u32 r;
	struct iris_frc_setting *frc_setting = &iris_info.frc_setting;
	struct iris_timing_info *output_timing = &iris_info.output_timing;;

	frc_setting->in_fps = data & 0xff;
	if (30 == frc_setting->in_fps)
		frc_setting->low_delay = (data >> 8) & 0xff;

	r = gcd(frc_setting->in_fps, output_timing->fps);
	frc_setting->in_ratio = frc_setting->in_fps / r;
	frc_setting->out_ratio = output_timing->fps / r;
        memc_input_fps = frc_setting->in_fps;
	pr_debug("iris2 %s, in_ratio = %d, out_ratio = %d\n", __func__, frc_setting->in_ratio, frc_setting->out_ratio);
}

static u32 iris_color_temp_x_get(u32 index)
{
	return iris_color_x_buf[index];
}

static void iris_cmlut_interpolation(u32 ratio, u32 *cmlut_base, u32 *cmlut_target)
{
	int i;
	u32 r_base, g_base, b_base;
	u32 r_target, g_target, b_target;
	u32 r_value, g_value, b_value;
	u32 *cm_lut = iris_info.lut_info.cmlut[CMI_CALCULATION];

	for(i=0; i<IRIS_CM_LUT_LENGTH; i++) {
		r_base = cmlut_base[i] & 0x000003ff;
		g_base = (cmlut_base[i] & 0x003ffc00) >> 10;
		b_base = (cmlut_base[i] & 0xffc00000) >> 22;
		r_target = cmlut_target[i] & 0x000003ff;
		g_target = (cmlut_target[i] & 0x003ffc00) >> 10;
		b_target = (cmlut_target[i] & 0xffc00000) >> 22;
		r_value = ((10000 - ratio)*r_target + ratio*r_base)/10000;
		g_value = ((10000 - ratio)*g_target + ratio*g_base)/10000;
		b_value = ((10000 - ratio)*b_target + ratio*b_base)/10000;
		cm_lut[i] = r_value + (g_value<<10) + (b_value<<22);
	}
}

void iris_cmlut_color_temp_set(u32 cctvalue, u32 cm_gamut)
{
	u32 index;
	u32 xvalue;
	u32 ratio;
	struct iris_lut_info *lut_info = &iris_info.lut_info;

	if(cctvalue > IRIS_CCT_MAX_VALUE)
		cctvalue = IRIS_CCT_MAX_VALUE;
	else if(cctvalue < IRIS_CCT_MIN_VALUE)
		cctvalue = IRIS_CCT_MIN_VALUE;
	index = (cctvalue - IRIS_CCT_MIN_VALUE)/100;
	xvalue = iris_color_temp_x_get(index);


	if ((xvalue >= IRIS_X_8000K)&&(xvalue < IRIS_X_6500K)) {
		ratio = ((xvalue - IRIS_X_8000K)*10000)/(IRIS_X_6500K - IRIS_X_8000K);
		pr_info( "%s: ratio= %d\n", __func__, ratio );
		switch(cm_gamut) {
			case CM_NTSC:
				iris_cmlut_interpolation(ratio, lut_info->cmlut[CMI_NTSC_6500], lut_info->cmlut[CMI_NTSC_8000]);
				break;
			case CM_sRGB:
				iris_cmlut_interpolation(ratio, lut_info->cmlut[CMI_SRGB_6500], lut_info->cmlut[CMI_SRGB_8000]);
				break;
			case CM_DCI_P3:
				iris_cmlut_interpolation(ratio, lut_info->cmlut[CMI_P3_6500], lut_info->cmlut[CMI_P3_8000]);
				break;
			default:
				break;
		}
	} else if ((xvalue <= IRIS_X_2800K)&&(xvalue >= IRIS_X_6500K)) {
		ratio = ((xvalue - IRIS_X_6500K)*10000)/(IRIS_X_2800K - IRIS_X_6500K);
		pr_info( "%s: ratio= %d\n", __func__, ratio );
		switch(cm_gamut) {
			case CM_NTSC:
				iris_cmlut_interpolation(ratio, lut_info->cmlut[CMI_NTSC_2800], lut_info->cmlut[CMI_NTSC_6500]);
				break;
			case CM_sRGB:
				iris_cmlut_interpolation(ratio, lut_info->cmlut[CMI_SRGB_2800], lut_info->cmlut[CMI_SRGB_6500]);
				break;
			case CM_DCI_P3:
				iris_cmlut_interpolation(ratio, lut_info->cmlut[CMI_P3_2800], lut_info->cmlut[CMI_P3_6500]);
				break;
			default:
				break;
		}
	}
}

static int iris_cmlut_table_read(int index, u32 target_addr_start, u32 *last_pkt_len)
{
	u32 table_len = IRIS_CM_LUT_LENGTH * 4;
	u32 pkt_len, pkt_num = 0;
	u8 *buf = NULL;
	u32 *cmlut = iris_info.lut_info.cmlut[index];

	if (!iris_cmlut_buf) {
		iris_cmlut_buf = kzalloc(32 * 1024, GFP_KERNEL);
		if (!iris_cmlut_buf) {
			pr_err("iris2 %s: failed to alloc cm lut mem, size = %d\n", __func__, 32 * 1024);
			return false;
		}
	}

	buf = iris_cmlut_buf;
	while (table_len) {
		if (table_len > IRIS_CMLUT_GRCP_PKT_SIZE) {
			pkt_len = IRIS_CMLUT_GRCP_PKT_SIZE;
		} else {
			pkt_len = table_len;
		}
		iris_cmlut_burst_grcp_header[8] = (pkt_len / 4) + 2;
		iris_cmlut_burst_grcp_header[14] = pkt_len / 4;
		memcpy(buf, iris_cmlut_burst_grcp_header, sizeof(iris_cmlut_burst_grcp_header));
		*(u32 *)(buf + 16) = cpu_to_le32(target_addr_start);
		memcpy (buf + 20, (u8 *)cmlut, IRIS_CMLUT_GRCP_PKT_SIZE);

		buf += IRIS_CMLUT_GRCP_PKT_SIZE + 20;
		cmlut += IRIS_CMLUT_GRCP_PKT_SIZE / 4;
		target_addr_start += IRIS_CMLUT_GRCP_PKT_SIZE;
		table_len -= pkt_len;
		pkt_num++;
	}
	if (!table_len)
		*last_pkt_len = pkt_len;

	return pkt_num;
}

static void iris_cmlut_table_grcp_send(struct dsi_panel *panel, u32 pkt_num, u32 last_pkt_len)
{
	static struct dsi_cmd_desc lut_send_cmd[IRIS_CMLUT_GRCP_PKT_NUM];
	u32 cmd_indx, pkt_indx;
	u32 pkt_len = IRIS_CMLUT_GRCP_PKT_SIZE + 20;

	for (pkt_indx = 0; pkt_indx < (pkt_num - 1); pkt_indx++) {
		cmd_indx = 0;//pkt_indx % IRIS_CMLUT_GRCP_PKT_NUM;
		lut_send_cmd[cmd_indx].last_command = 0;
		lut_send_cmd[cmd_indx].msg.type = MIPI_DSI_GENERIC_LONG_WRITE;
		lut_send_cmd[cmd_indx].msg.tx_len = pkt_len;
		lut_send_cmd[cmd_indx].msg.tx_buf = iris_cmlut_buf + pkt_indx * pkt_len;
		if (cmd_indx == (IRIS_CMLUT_GRCP_PKT_NUM - 1)) {
			lut_send_cmd[cmd_indx].last_command = 1;
			iris_dsi_cmds_send(panel, lut_send_cmd, IRIS_CMLUT_GRCP_PKT_NUM, DSI_CMD_SET_STATE_HS);
		}
	}
	cmd_indx = 0;//(pkt_num % IRIS_CMLUT_GRCP_PKT_NUM) - 1;
	lut_send_cmd[cmd_indx].last_command = 1;
	lut_send_cmd[cmd_indx].msg.type = MIPI_DSI_GENERIC_LONG_WRITE;
	lut_send_cmd[cmd_indx].msg.tx_len = last_pkt_len + 20;
	lut_send_cmd[cmd_indx].msg.tx_buf = iris_cmlut_buf + (pkt_num - 1) * pkt_len;

	iris_dsi_cmds_send(panel, lut_send_cmd, 1, DSI_CMD_SET_STATE_HS); //todo
}

void iris_cmlut_grcp_set(struct dsi_panel *panel, int index, u32 target_addr_start)
{
	u32 last_pkt_len = 0, pkt_num;

	pkt_num = iris_cmlut_table_read(index, target_addr_start, &last_pkt_len);
	if (pkt_num)
		iris_cmlut_table_grcp_send(panel, pkt_num, last_pkt_len);
}

void iris_cmlut_table_load(struct dsi_panel *panel)
{
	struct iris_lut_info *lut_info = &iris_info.lut_info;
	struct feature_setting *chip_setting = &iris_info.chip_setting;
	u32 temp_value = chip_setting->cm_setting.color_temp * 100;
	u32 temp_adjust = chip_setting->cm_setting.color_temp_adjust * 100;

	if (!lut_info->lut_fw_state)
		return;

	if (chip_setting->cm_setting.color_temp_en) {
		if (chip_setting->cm_setting.sensor_auto_en) {
			if (temp_value < IRIS_CCT_MIN_VALUE)
				temp_value = IRIS_CCT_MIN_VALUE;
			else if (temp_value > IRIS_CCT_MAX_VALUE)
				temp_value = IRIS_CCT_MAX_VALUE;
			temp_value = temp_value + temp_adjust - 3200;
			iris_cmlut_color_temp_set(temp_value, chip_setting->cm_setting.cm3d);
			iris_cmlut_grcp_set(panel, CMI_CALCULATION, (u32)IRIS_CMLUT_IP_ADDR);
		}
	}
}

void iris_gamma_table_reg_add(u32 addr, u32 val, bool last)
{
	static u32 gamma_len = 0;
	static struct dsi_cmd_desc iris_gamma_cmd = {
		{0, MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, 0, CMD_PKT_SIZE, gamma_cmd.cmd, 1, iris_read_cmd_buf}, 1, 0};

	static u32 reg_indx = 0;

	if (0 == reg_indx) {
		memset(&gamma_cmd, 0, sizeof(gamma_cmd));
		memcpy(gamma_cmd.cmd, grcp_header, GRCP_HEADER);
		gamma_cmd.cmd_len = GRCP_HEADER;
	}

	iris_cmd_reg_add(&gamma_cmd, addr, val);
	reg_indx++;

	if ((25 == reg_indx) || last) {
		gamma_len = (gamma_cmd.cmd_len - GRCP_HEADER) / 4;
		*(u32 *)(gamma_cmd.cmd + 8) = cpu_to_le32(gamma_len + 1);
		*(u16 *)(gamma_cmd.cmd + 14) = cpu_to_le16(gamma_len);

		iris_gamma_cmd.msg.tx_len = gamma_cmd.cmd_len;
		iris_dsi_cmds_send(iris_panel, &iris_gamma_cmd, 1, DSI_CMD_SET_STATE_HS);

		reg_indx = 0;
		//iris_dump_packet1(gamma_cmd.cmd, gamma_cmd.cmd_len);
		//pr_err("iris2 gamma cmd: %d %d\n", reg_indx, gamma_cmd.cmd_len);

	}
}

void iris_gamma_table_update(struct feature_setting *chip_setting)
{
	u32 dither_mode, Gamma_size, width, length;
	u32 *table_buf = (u32 *)gamma_fw_buf;
	u32 entry = 0;
	u32 index = 0;

	if (!iris_info.gamma_info.gamma_fw_state || !gamma_fw_buf)
		return;

	dither_mode = *table_buf++;
	Gamma_size = *table_buf++;
	width = *table_buf++;
	length = *table_buf++;

	if (chip_setting->hdr_setting.hdren)
		index = 3;
	else if (chip_setting->cm_setting.cm3d == CM_NTSC)
		index = 0;
	else if (chip_setting->cm_setting.cm3d == CM_sRGB)
		index = 1;
	else if (chip_setting->cm_setting.cm3d == CM_DCI_P3)
		index = 2;
	else
		index = 0;

	table_buf += index * length * 3;

	if (!chip_setting->gamma_enable) {
		iris_gamma_table_reg_add(0xf1580000, Gamma_size + (dither_mode << 2) + (((width == 12) ? 0 : 1) << 15), false);
		iris_gamma_table_reg_add(0xf159ffd4, 1, true);
		pr_info("iris2 update iris gamma table: disable\n");
		return;
	}

	//pr_err("iris2 %d, %d, %d, %d\n", dither_mode, Gamma_size, width, length);
	iris_gamma_table_reg_add(0xf1580000, Gamma_size + (dither_mode << 2) + (((width == 12) ? 0 : 1) << 15), false);
	iris_gamma_table_reg_add(0xf159ffd4, 1, false);

	iris_gamma_table_reg_add(0xf1590010, 0, false);
	iris_gamma_table_reg_add(0xf1590014, 0, false);
	iris_gamma_table_reg_add(0xf1590018, 0, false);

	for (entry = 0; entry < length; entry++) {
		iris_gamma_table_reg_add(0xf1590000, *table_buf, false);
		iris_gamma_table_reg_add(0xf1590004, *(table_buf + length), false);
		iris_gamma_table_reg_add(0xf1590008, *(table_buf + length * 2), false);
		pr_debug("iris2 %d %d %d\n", *table_buf, *(table_buf + length), *(table_buf + length * 2));
		table_buf++;
	}

	iris_gamma_table_reg_add(0xf1580000, Gamma_size + (dither_mode << 2) + (1 << 6) + (((width == 12) ? 0 : 1) << 15), false);
	iris_gamma_table_reg_add(0xf159ffd4, 1, true);

	pr_err("iris2 update iris gamma table: %d\n", index);
}


void iris_mipi_reg_write(u32 addr, u32 value)
{
	static char reg_write[24] = {
		PWIL_TAG('P', 'W', 'I', 'L'),
		PWIL_TAG('G', 'R', 'C', 'P'),
		PWIL_U32(0x3),
		0x00,
		0x00,
		PWIL_U16(0x2),
		PWIL_U32(IRIS_PROXY_MB0_ADDR), //default set to proxy MB0
		PWIL_U32(0x00000000)
	};
	struct dsi_cmd_desc iris_reg_write_cmd = {
		{0, MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, 0, sizeof(reg_write), reg_write, 1, iris_read_cmd_buf}, 1, 0};

	*(u32 *)(reg_write + 16) = cpu_to_le32(addr);
	*(u32 *)(reg_write + 20) = cpu_to_le32(value);

	iris_dsi_cmds_send(iris_panel, &iris_reg_write_cmd, 1, DSI_CMD_SET_STATE_HS);
}

int iris_mipi_reg_read(u32 addr, u32 *value)
{
	char reg_address[16] = {
		PWIL_TAG('P', 'W', 'I', 'L'),
		PWIL_TAG('S', 'G', 'L', 'W'),
		PWIL_U32(0x01),	//valid body word(4bytes)
		PWIL_U32(IRIS_PROXY_MB0_ADDR),   // proxy MB0
	};
	char reg_read[1] = {0x0};
	const struct mipi_dsi_host_ops *ops = iris_panel->host->ops;
	struct dsi_cmd_desc reg_write_cmd = {
		{0, MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, 0, sizeof(reg_address), reg_address, 1, iris_read_cmd_buf}, 1, 0};
	struct dsi_cmd_desc reg_read_cmds = {
		{0, MIPI_DSI_GENERIC_READ_REQUEST_1_PARAM, 0, 0, sizeof(reg_read), reg_read, 4, iris_read_cmd_buf}, 1, 0};
	int rc = 0;

	*(u32 *)(reg_address + 12) = cpu_to_le32(addr);
	iris_dsi_cmds_send(iris_panel, &reg_write_cmd, 1, DSI_CMD_SET_STATE_HS);

	memset(iris_read_cmd_buf, 0, sizeof(iris_read_cmd_buf));
	reg_read_cmds.msg.flags |= MIPI_DSI_MSG_LASTCOMMAND;
	reg_read_cmds.msg.flags |= BIT(6);

	rc = ops->transfer(iris_panel->host, &reg_read_cmds.msg);
	if (rc < 0) {
		pr_err("iris2 failed to set cmds(%d), rc=%d\n", reg_read_cmds.msg.type, rc);
		*value = 0xffffffff;
		return rc;
	}
	*value = iris_read_cmd_buf[0] | (iris_read_cmd_buf[1] << 8)
			| (iris_read_cmd_buf[2] << 16) | (iris_read_cmd_buf[3] << 24);

	return rc;
}

void iris_cmlut_table_update(struct dsi_panel *panel, u32 cm3d)
{
	struct iris_lut_info *lut_info = &iris_info.lut_info;

	if (!lut_info->lut_fw_state)
		return;

	switch (cm3d) {
		case CM_NTSC:		//table for NTSC
		iris_cmlut_grcp_set(panel, CMI_NTSC_2800, (u32)IRIS_CMLUT_2800K_ADDR);
		iris_cmlut_grcp_set(panel, CMI_NTSC_6500_C51_ENDIAN, (u32)IRIS_CMLUT_6500K_ADDR);
		iris_cmlut_grcp_set(panel, CMI_NTSC_8000, (u32)IRIS_CMLUT_8000K_ADDR);
		break;
		case CM_sRGB:		//table for SRGB
		iris_cmlut_grcp_set(panel, CMI_SRGB_2800, (u32)IRIS_CMLUT_2800K_ADDR);
		iris_cmlut_grcp_set(panel, CMI_SRGB_6500_C51_ENDIAN, (u32)IRIS_CMLUT_6500K_ADDR);
		iris_cmlut_grcp_set(panel, CMI_SRGB_8000, (u32)IRIS_CMLUT_8000K_ADDR);
		break;
		case CM_DCI_P3:		//table for DCI-P3
		iris_cmlut_grcp_set(panel, CMI_P3_2800, (u32)IRIS_CMLUT_2800K_ADDR);
		iris_cmlut_grcp_set(panel, CMI_P3_6500_C51_ENDIAN, (u32)IRIS_CMLUT_6500K_ADDR);
		iris_cmlut_grcp_set(panel, CMI_P3_8000, (u32)IRIS_CMLUT_8000K_ADDR);
		break;
		case CM_HDR:       //table for HDR
		iris_cmlut_grcp_set(panel, CMI_HDR_2800, (u32)IRIS_CMLUT_2800K_ADDR);
		iris_cmlut_grcp_set(panel, CMI_HDR_6500_C51_ENDIAN, (u32)IRIS_CMLUT_6500K_ADDR);
		iris_cmlut_grcp_set(panel, CMI_HDR_8000, (u32)IRIS_CMLUT_8000K_ADDR);
		break;
	}

}


void iris_datapath_csc_enable(u32 hdr_en)
{
	u32 grcp_len = 0;
	struct dsi_cmd_desc iris_grcp_cmd = {
		{0, MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, 0, CMD_PKT_SIZE, grcp_cmd.cmd, 1, iris_read_cmd_buf}, 1, 0};

	memset(&grcp_cmd, 0, sizeof(grcp_cmd));
	memcpy(grcp_cmd.cmd, grcp_header, GRCP_HEADER);
	grcp_cmd.cmd_len = GRCP_HEADER;

	if (hdr_en) {
		//pwil CSC
		iris_cmd_reg_add(&grcp_cmd, 0xf1240304, 0x08000000);
		iris_cmd_reg_add(&grcp_cmd, 0xf1240308, 0x00000000);
		iris_cmd_reg_add(&grcp_cmd, 0xf124030c, 0x08000000);
		iris_cmd_reg_add(&grcp_cmd, 0xf1240310, 0x00000800);
		iris_cmd_reg_add(&grcp_cmd, 0xf1240314, 0x00000000);
		iris_cmd_reg_add(&grcp_cmd, 0xf1240318, 0x00000000);
		iris_cmd_reg_add(&grcp_cmd, 0xf124031c, 0x00000000);
		//peaking CSC
		iris_cmd_reg_add(&grcp_cmd, 0xf1a04004, 0x08000000);
		iris_cmd_reg_add(&grcp_cmd, 0xf1a04008, 0x00000000);
		iris_cmd_reg_add(&grcp_cmd, 0xf1a0400c, 0x08000000);
		iris_cmd_reg_add(&grcp_cmd, 0xf1a04010, 0x00000800);
		iris_cmd_reg_add(&grcp_cmd, 0xf1a04014, 0x00000000);
		iris_cmd_reg_add(&grcp_cmd, 0xf1a04018, 0x00000000);
		iris_cmd_reg_add(&grcp_cmd, 0xf1a0401c, 0x00000000);
		//cm CSC
		iris_cmd_reg_add(&grcp_cmd, 0xf1560100, 0x0000000f);
		iris_cmd_reg_add(&grcp_cmd, 0xf1560104, 0x09510951);
		iris_cmd_reg_add(&grcp_cmd, 0xf1560108, 0x00000951);
		iris_cmd_reg_add(&grcp_cmd, 0xf156010c, 0x11227e80);
		iris_cmd_reg_add(&grcp_cmd, 0xf1560110, 0x7acc0d6e);
		//registers
		iris_cmd_reg_add(&grcp_cmd, 0xf1241000, 0xc9100010);
		iris_cmd_reg_add(&grcp_cmd, 0xf1560000, 0x8820e000);
		//update
		iris_cmd_reg_add(&grcp_cmd, 0xf157ffd0, 0x00000100);
		//iris_reg_add(0xf1a1ff00, 0x00000100);peaking CSC doesn't need update.
		iris_cmd_reg_add(&grcp_cmd, 0xf1250000, 0x00000100);
	} else {
		iris_cmd_reg_add(&grcp_cmd, 0xf1240304, 0x04b27ca7);
		iris_cmd_reg_add(&grcp_cmd, 0xf1240308, 0x7f5a7d5a);
		iris_cmd_reg_add(&grcp_cmd, 0xf124030c, 0x03ff00e9);
		iris_cmd_reg_add(&grcp_cmd, 0xf1240310, 0x026503ff);
		iris_cmd_reg_add(&grcp_cmd, 0xf1240314, 0x00007ea7);
		iris_cmd_reg_add(&grcp_cmd, 0xf1240318, 0x00000800);
		iris_cmd_reg_add(&grcp_cmd, 0xf124031c, 0x00000800);

		iris_cmd_reg_add(&grcp_cmd, 0xf1a04004, 0x04b27ca7);
		iris_cmd_reg_add(&grcp_cmd, 0xf1a04008, 0x7f5a7d5a);
		iris_cmd_reg_add(&grcp_cmd, 0xf1a0400c, 0x03ff00e9);
		iris_cmd_reg_add(&grcp_cmd, 0xf1a04010, 0x026503ff);
		iris_cmd_reg_add(&grcp_cmd, 0xf1a04014, 0x00007ea7);
		iris_cmd_reg_add(&grcp_cmd, 0xf1a04018, 0x00000800);
		iris_cmd_reg_add(&grcp_cmd, 0xf1a0401c, 0x00000800);

		iris_cmd_reg_add(&grcp_cmd, 0xf1560100, 0x0000000d);
		iris_cmd_reg_add(&grcp_cmd, 0xf1560104, 0x08000800);
		iris_cmd_reg_add(&grcp_cmd, 0xf1560108, 0x00000800);
		iris_cmd_reg_add(&grcp_cmd, 0xf156010c, 0x0e307d40);
		iris_cmd_reg_add(&grcp_cmd, 0xf1560110, 0x7a480b38);
		//registers
		iris_cmd_reg_add(&grcp_cmd, 0xf1241000, 0xe4100010);
		iris_cmd_reg_add(&grcp_cmd, 0xf1560000, 0x0820e000);//Sephy debug here
		//update
		iris_cmd_reg_add(&grcp_cmd, 0xf157ffd0, 0x00000100);
		//iris_reg_add(0xf1a1ff00, 0x00000100);
		iris_cmd_reg_add(&grcp_cmd, 0xf1250000, 0x00000100);
	}

	grcp_len = (grcp_cmd.cmd_len - GRCP_HEADER) / 4;
	*(u32 *)(grcp_cmd.cmd + 8) = cpu_to_le32(grcp_len + 1);
	*(u16 *)(grcp_cmd.cmd + 14) = cpu_to_le16(grcp_len);

	iris_grcp_cmd.msg.tx_len = grcp_cmd.cmd_len;
	iris_dsi_cmds_send(iris_panel, &iris_grcp_cmd, 1, DSI_CMD_SET_STATE_HS);
	pr_info("iris2 update csc: %d\n", hdr_en);
}

int iris_configure(u32 type, u32 value)
{
#if 1
	struct feature_setting *user_setting = & iris_info.user_setting;
	struct iris_setting_update *settint_update = &iris_info.settint_update;

	//u32 configAddr = 0;
	//u32 configValue = 0;

	pr_info("iris2 iris_configure: %d - 0x%x\n", type, value);

	if (type >= IRIS_CONFIG_TYPE_MAX)
		return -EINVAL;
	//mutex_lock(&iris_info.config_mutex);
	switch (type) {
	case IRIS_PEAKING:
		user_setting->pq_setting.peaking = value & 0xf;
		user_setting->pq_setting.update = 1;
		settint_update->pq_setting = true;
		break;
	case IRIS_SHARPNESS:
		user_setting->pq_setting.sharpness = value & 0xf;
		user_setting->pq_setting.update = 1;
		settint_update->pq_setting = true;
		break;
	case IRIS_MEMC_DEMO:
		user_setting->pq_setting.memcdemo = value & 0xf;
		user_setting->pq_setting.update = 1;
		settint_update->pq_setting = true;
		break;
	case IRIS_PEAKING_DEMO:
		user_setting->pq_setting.peakingdemo = value & 0xf;
		user_setting->pq_setting.update = 1;
		settint_update->pq_setting = true;
		break;
	case IRIS_GAMMA:
		user_setting->pq_setting.gamma = value & 0x3;
		user_setting->pq_setting.update = 1;
		settint_update->pq_setting = true;
		break;
	case IRIS_MEMC_LEVEL:
		user_setting->pq_setting.memclevel = value & 0x3;
		user_setting->pq_setting.update = 1;
		settint_update->pq_setting = true;
		break;
	case IRIS_CONTRAST:
		user_setting->pq_setting.contrast = value & 0xff;
		user_setting->pq_setting.update = 1;
		settint_update->pq_setting = true;
		break;
	case IRIS_BRIGHTNESS:
		user_setting->dbc_setting.brightness = value & 0x7f;
		user_setting->dbc_setting.update = 1;
		settint_update->dbc_setting = true;
		break;
	case IRIS_EXTERNAL_PWM:
		user_setting->dbc_setting.ext_pwm = value & 0x1;
		user_setting->dbc_setting.update = 1;
		settint_update->dbc_setting = true;
		break;
	case IRIS_DBC_QUALITY:
		user_setting->dbc_setting.cabcmode = value & 0xf;
		user_setting->dbc_setting.update = 1;
		settint_update->dbc_setting = true;
		break;
	case IRIS_DLV_SENSITIVITY:
		user_setting->dbc_setting.dlv_sensitivity = value & 0xfff;
		user_setting->dbc_setting.update = 1;
		settint_update->dbc_setting = true;
		break;
	case IRIS_DBC_CONFIG:
		user_setting->dbc_setting = *((struct iris_dbc_setting *)&value);
		user_setting->dbc_setting.update = 1;
		settint_update->dbc_setting = true;
		break;
	case IRIS_PQ_CONFIG:
		value |= user_setting->pq_setting.cinema_en << 24;
		user_setting->pq_setting = *((struct iris_pq_setting *)&value);
		user_setting->pq_setting.update = 1;
		settint_update->pq_setting = true;
		break;
	case IRIS_LPMEMC_CONFIG:
		user_setting->lp_memc_setting.level = value;
		user_setting->lp_memc_setting.value = iris_lp_memc_calc(value);
		settint_update->lp_memc_setting = true;
		break;
	case IRIS_COLOR_ADJUST:
		user_setting->color_adjust = value & 0xff;
		settint_update->color_adjust = true;
		break;
	case IRIS_LCE_SETTING:
		user_setting->lce_setting.mode = value & 0xf;
		user_setting->lce_setting.mode1level = (value & 0xf0) >> 4;
		user_setting->lce_setting.mode2level = (value & 0xf00) >> 8;
		user_setting->lce_setting.demomode = (value & 0xf000) >> 12;
		user_setting->lce_setting.graphics_detection = (value & 0x10000) >> 16;
		user_setting->lce_setting.update = 1;
		settint_update->lce_setting = true;
		break;
	case IRIS_CM_SETTING:
		user_setting->cm_setting.cm6axes = value & 0x07;
		user_setting->cm_setting.cm3d = (value & 0xf8) >> 3;
		user_setting->cm_setting.demomode = (value & 0x700) >> 8;
		user_setting->cm_setting.ftc_en = (value & 0x800) >> 11;
		user_setting->cm_setting.color_temp_en = (value & 0x1000) >> 12;
		user_setting->cm_setting.color_temp = (value & 0xfffe000) >> 13;
		user_setting->cm_setting.color_temp_adjust = (value & 0xfffe000) >> 22;
		user_setting->cm_setting.sensor_auto_en = (value & 0x10000000) >> 28;
		user_setting->cm_setting.update = 1;
		if (iris_info.lut_info.lut_fw_state && iris_info.gamma_info.gamma_fw_state)
			settint_update->cm_setting = true;
		else
			iris_cm_setting_force_update = true;
		break;
	case IRIS_CINEMA_MODE:
		user_setting->pq_setting.cinema_en = value & 0x1;
		user_setting->pq_setting.update = 1;
		settint_update->pq_setting = true;
		break;
	case IRIS_DBG_TARGET_PI_REGADDR_SET:
		iris_reg_addr = value;
		break;
	case IRIS_DBG_TARGET_REGADDR_VALUE_SET:
		if (iris_panel->panel_initialized)
			#ifdef I2C_ENABLE
				iris_i2c_reg_write(iris_reg_addr, value);
			#else
				iris_mipi_reg_write(iris_reg_addr, value);
			#endif
		break;
	case IRIS_LUX_VALUE:
		user_setting->lux_value.luxvalue= value & 0xffff;
		user_setting->lux_value.update = 1;
		settint_update->lux_value = true;
		break;
	case IRIS_CCT_VALUE:
		user_setting->cct_value.cctvalue= value & 0xffff;
		user_setting->cct_value.update = 1;
		settint_update->cct_value = true;
		break;
	case IRIS_READING_MODE:
		user_setting->reading_mode.readingmode = value;
		user_setting->reading_mode.update = 1;
		settint_update->reading_mode = true;
		break;
	case IRIS_HDR_SETTING:
		user_setting->hdr_setting.hdrlevel = value & 0xff;
		user_setting->hdr_setting.hdren = (value & 0x100) >> 8;
		user_setting->hdr_setting.update = 1;
		settint_update->hdr_setting = true;
		break;
	case IRIS_GAMMA_TABLE_EN:
		user_setting->gamma_enable = value;
		settint_update->gamma_table = true;
		break;

	default:
		//mutex_unlock(&iris_cfg->config_mutex);
		return -EINVAL;
	}

	#if 0
	if (1) {
		if (settint_update->lux_value) {
			iris_reg_add(IRIS_LUX_VALUE_ADDR, user_setting->lux_value.luxvalue);
			settint_update->lux_value= false;
			mutex_unlock(&iris_cfg->config_mutex);
			return 0;
		} else if (iris_info.update.cct_value) {
			iris_reg_add(IRIS_CCT_VALUE_ADDR, user_setting->cct_value.cctvalue);
			iris_info.update.cct_value= false;
			mutex_unlock(&iris_cfg->config_mutex);
			return 0;
		}
	}
	#endif
	//mutex_unlock(&iris_info.config_mutex);
#endif
	return 0;

}

static int iris_oprt_configure(u32 type, void __user *argp)
{
	int ret = -1;
	uint32_t value = 0;
	ret = copy_from_user(&value, argp, sizeof(uint32_t));
	if (ret)
		return ret;
	ret = iris_configure(type, value);
	return ret;
}

void iris_demo_window_update(struct demo_win_info *pdemo_win_info)
{
	int frcEndx, frcEndy, frcStartx, frcStarty;
	int color = 0, colsize = 0, rowsize = 0, modectrl = 0x3f00;
	int displaywidth = iris_info.output_timing.hres;
	int displayheight = iris_info.output_timing.vres;

	if (pdemo_win_info->fi_demo_en) {
		frcStartx = pdemo_win_info->startx * lp_memc_timing[0] / displaywidth;
		frcStarty = pdemo_win_info->starty * lp_memc_timing[1] / displayheight;
		frcEndx = pdemo_win_info->endx *  lp_memc_timing[0] / displaywidth;
		frcEndy = pdemo_win_info->endy *  lp_memc_timing[1] / displayheight;
		if (frcEndy + pdemo_win_info->borderwidth >= lp_memc_timing[1])
			frcEndy = lp_memc_timing[1] - pdemo_win_info->borderwidth;

		pr_debug("iris2 frc mode resolution: %d - %d - %d - %d - %d - %d\n", 
			frcStartx, frcStarty, frcEndx, frcEndy, lp_memc_timing[0], lp_memc_timing[1]);

		color = pdemo_win_info->color;
		colsize = (frcStartx & 0xfff) | ((frcEndx & 0xfff)<<16);
		rowsize = (frcStarty & 0xfff) | ((frcEndy & 0xfff)<<16);

		modectrl = modectrl | pdemo_win_info->fi_demo_en;
		modectrl = modectrl | 1<<1;
		modectrl = modectrl | ((pdemo_win_info->borderwidth & 0x7)<<4);

		pr_debug("iris2 %s: COL_SIZE =%x, MODE_RING=%x, ROW_SIZE=%x, MODE_CTRL=%x\n",
			__func__, colsize, color, rowsize, modectrl);

		iris_info.fi_demo_win_info.startx = pdemo_win_info->startx;
		iris_info.fi_demo_win_info.starty = pdemo_win_info->starty;
		iris_info.fi_demo_win_info.endx = pdemo_win_info->endx;
		iris_info.fi_demo_win_info.endy = pdemo_win_info->endy;
		iris_info.fi_demo_win_info.borderwidth = pdemo_win_info->borderwidth;

		iris_info.fi_demo_win_info.colsize = colsize;
		iris_info.fi_demo_win_info.color = color;
		iris_info.fi_demo_win_info.rowsize = rowsize;
		iris_info.fi_demo_win_info.modectrl = modectrl;
		iris_info.settint_update.demo_win_fi = true;
	}

	if (pdemo_win_info->sharpness_en) {
		iris_info.peaking_demo_win_info.startx = pdemo_win_info->startx;
		iris_info.peaking_demo_win_info.starty = pdemo_win_info->starty;
		iris_info.peaking_demo_win_info.endx = pdemo_win_info->endx;
		iris_info.peaking_demo_win_info.endy = pdemo_win_info->endy;
		iris_info.peaking_demo_win_info.sharpness_en = pdemo_win_info->sharpness_en;
		iris_info.settint_update.demo_win_peaking = true;
	}

	if (pdemo_win_info->cm_demo_en) {
		iris_info.cm_demo_win_info.startx = pdemo_win_info->startx;
		iris_info.cm_demo_win_info.starty = pdemo_win_info->starty;
		iris_info.cm_demo_win_info.endx = pdemo_win_info->endx;
		iris_info.cm_demo_win_info.endy = pdemo_win_info->endy;
		iris_info.cm_demo_win_info.cm_demo_en = pdemo_win_info->cm_demo_en;
		iris_info.settint_update.demo_win_cm = true;
	}
}

void iris_demo_window_set(void)
{
	int winstart = 0, winend = 0;
	struct peaking_demo_win *peaking_demo_win_info = &iris_info.peaking_demo_win_info;
	struct cm_demo_win *cm_demo_win_info = &iris_info.cm_demo_win_info;
	u32 peakingctrl = 0;

	if (iris_info.settint_update.demo_win_fi) {
		iris_info.settint_update.demo_win_fi = false;
		iris_cmd_reg_add(&meta_cmd, FI_DEMO_COL_SIZE, iris_info.fi_demo_win_info.colsize);
		iris_cmd_reg_add(&meta_cmd, FI_DEMO_MODE_RING, iris_info.fi_demo_win_info.color);
		iris_cmd_reg_add(&meta_cmd, FI_DEMO_ROW_SIZE, iris_info.fi_demo_win_info.rowsize);
		iris_cmd_reg_add(&meta_cmd, FI_DEMO_MODE_CTRL, iris_info.fi_demo_win_info.modectrl);
		iris_cmd_reg_add(&meta_cmd, FI_SHADOW_UPDATE, 1);
	}

	if (iris_info.settint_update.demo_win_peaking) {
		iris_info.settint_update.demo_win_peaking = false;
		winstart = (peaking_demo_win_info->startx & 0x3fff) + ((peaking_demo_win_info->starty & 0x3fff) << 16);
		winend =  (peaking_demo_win_info->endx & 0x3fff) + ((peaking_demo_win_info->endy & 0x3fff) << 16);
		peakingctrl = 3;
		iris_cmd_reg_add(&meta_cmd, PEAKING_STARTWIN, winstart);
		iris_cmd_reg_add(&meta_cmd, PEAKING_ENDWIN, winend);
		iris_cmd_reg_add(&meta_cmd, PEAKING_CTRL, peakingctrl);
		iris_cmd_reg_add(&meta_cmd, PEAKING_SHADOW_UPDATE, 1);
	}

	if (iris_info.settint_update.demo_win_cm) {
		iris_info.settint_update.demo_win_cm = false;
		winstart = (cm_demo_win_info->startx & 0x3fff) + ((cm_demo_win_info->starty & 0x3fff) << 16);
		winend =  (cm_demo_win_info->endx & 0x3fff) + ((cm_demo_win_info->endy & 0x3fff) << 16);
		iris_cmd_reg_add(&meta_cmd, CM_STARTWIN, winstart);
		iris_cmd_reg_add(&meta_cmd, CM_ENDWIN, winend);
		iris_cmd_reg_add(&meta_cmd, CM_SHADOW_UPDATE, 1);
	}
}


int iris_configure_ex(u32 type, u32 count, u32 *values)
{
	struct demo_win_info *pdemo_win_info;
	int displaywidth = iris_info.output_timing.hres;
	int displayheight = iris_info.output_timing.vres;

	if(count <= 1)
		return	iris_configure(type, *values);

	pdemo_win_info = (struct demo_win_info *)values;
	if ((pdemo_win_info->startx >  displaywidth)
		|| (pdemo_win_info->starty > displayheight)
		|| (pdemo_win_info->endx >	displaywidth)
		|| (pdemo_win_info->endy > displayheight)
		|| (pdemo_win_info->startx >  pdemo_win_info->endx)
		|| (pdemo_win_info->starty >  pdemo_win_info->endy)) {
		pr_err("iris2 demo window info is incorrect\n");
		return -EINVAL;
	}

	memcpy(&iris_demo_win_info, values, sizeof(struct demo_win_info));
	iris_demo_window_update(&iris_demo_win_info);

	return 0;
}

static int iris_oprt_configure_ex(u32 type, u32 count, void __user *values)
{
	int ret = -1;
	u32 *val = NULL;

	val = kmalloc(sizeof(u32) * count, GFP_KERNEL);
	if (!val) {
		pr_err("iris2 can not kmalloc space\n");
		return -ENOSPC;
	}
	ret = copy_from_user(val, values, sizeof(u32) * count);
	if (ret) {
		kfree(val);
		return ret;
	}

	ret = iris_configure_ex(type, count, val);

	kfree(val);
	return ret;
}

static int iris_configure_get(u32 type, u32 count, u32 *values)
{
	int ret = 0;
	struct feature_setting *chip_setting = & iris_info.chip_setting;

	if (type >= IRIS_CONFIG_TYPE_MAX)
		return -EFAULT;

	switch (type) {
	case IRIS_PEAKING:
		*values = chip_setting->pq_setting.peaking;
		break;
	case IRIS_SHARPNESS:
		*values = chip_setting->pq_setting.sharpness;
		break;
	case IRIS_MEMC_DEMO:
		*values = chip_setting->pq_setting.memcdemo;
		break;
	case IRIS_PEAKING_DEMO:
		*values = chip_setting->pq_setting.peakingdemo;
		break;
	case IRIS_GAMMA:
		*values = chip_setting->pq_setting.gamma;
		break;
	case IRIS_MEMC_LEVEL:
		*values = chip_setting->pq_setting.memclevel;
		break;
	case IRIS_CONTRAST:
		*values = chip_setting->pq_setting.contrast;
		break;
	case IRIS_BRIGHTNESS:
		*values = chip_setting->pq_setting.sharpness;
		break;
	case IRIS_EXTERNAL_PWM:
		*values = chip_setting->dbc_setting.ext_pwm;
		break;
	case IRIS_DBC_QUALITY:
		*values = chip_setting->dbc_setting.cabcmode;
		break;
	case IRIS_DLV_SENSITIVITY:
		*values = chip_setting->dbc_setting.dlv_sensitivity;
		break;
	case IRIS_DBC_CONFIG:
		*values = *((u32 *)&chip_setting->dbc_setting);
		break;
	case IRIS_CINEMA_MODE:
		*values = chip_setting->pq_setting.cinema_en;
		break;
	case IRIS_LCE_SETTING:
		*values = *((u32 *)&chip_setting->lce_setting);
		break;
	case IRIS_CM_SETTING:
		*values = *((u32 *)&chip_setting->cm_setting);
		break;
	case IRIS_HDR_SETTING:
		*values = *((u32 *)&chip_setting->hdr_setting);
		break;
	case IRIS_COLOR_ADJUST:
		*values = chip_setting->color_adjust & 0xff;
		break;
	case IRIS_PQ_CONFIG:
		*values = *((u32 *)&chip_setting->pq_setting);
		break;
	case IRIS_LPMEMC_CONFIG:
		*values = chip_setting->lp_memc_setting.level;
		break;
	case IRIS_USER_DEMO_WND:
		memcpy(values, &iris_demo_win_info, count * sizeof(u32));
		break;
	case  IRIS_CHIP_VERSION:
		*values = 1;
		break;
	case IRIS_LUX_VALUE:
		*values = chip_setting->lux_value.luxvalue;
		break;
	case IRIS_CCT_VALUE:
		*values = chip_setting->cct_value.cctvalue;
		break;
	case IRIS_READING_MODE:
		*values = chip_setting->reading_mode.readingmode;
		break;
	case IRIS_GAMMA_TABLE_EN:
		*values = chip_setting->gamma_enable;
		break;
	case IRIS_DBG_TARGET_REGADDR_VALUE_GET:
		if (iris_panel->panel_initialized)
			#ifdef I2C_ENABLE
				iris_i2c_reg_read(iris_reg_addr, values);
			#else
				iris_mipi_reg_read(iris_reg_addr, values);
			#endif
		else
			*values = 0xffffffff;
		break;
	default:
		ret = -EFAULT;
	}

	return ret;
}

static int iris_oprt_configure_get(u32 type, u32 count, void __user *argp)
{
	int ret = -1;
	u32 *val = NULL;

	val = kmalloc(count * sizeof(u32), GFP_KERNEL);
	if (val == NULL) {
		pr_err("iris2 could not kmalloc space for func = %s\n", __func__);
		return -ENOSPC;
	}

	ret = iris_configure_get(type, count, val);
	if (ret) {
		pr_err("iris2 get error\n");
		kfree(val);
		return -EPERM;
	}

	ret = copy_to_user(argp, val, sizeof(u32) * count);
	if (ret) {
		pr_err("iris2 copy to user error\n");
		kfree(val);
		return -EPERM;
	}

	kfree(val);
	return ret;
}

static int iris_oprt_get_lcd_calib_data(u32 type, u32 count, void __user *argp)
{
	int ret = -1;
	uint8_t *val = NULL;
	uint8_t *data = NULL;
	int size = count;
	int i = 0;

	val = kmalloc(count * sizeof(uint8_t), GFP_KERNEL);
	if (val == NULL) {
		pr_err("iris2 could not kmalloc space for func = %s\n", __func__);
		return -ENOSPC;
	}

	get_lcd_calibrate_data(&data, &size);
	if (data) {
		for (i = 0; i<count; i++) {
			if (0xff != data[i]) {
				break;
			}
		}
		if (count == i) {
			pr_err("iris2 lcd calibrate data invalid\n");
			kfree(val);
			return -ENOSPC;
		} else {
			memcpy(val, data, count);
			for (i = 0; i<count; i++)
				pr_err("iris2 0x%x cal data 0x%x\n", i, val[i]);
		}
	} else {
		pr_err("iris2 lcd calibrate data err %p \n", data);
		kfree(val);
		return -ENOSPC;
	}

	ret = copy_to_user(argp, val, sizeof(uint8_t) * count);
	if (ret) {
		pr_err("iris2 copy to user error\n");
		kfree(val);
		return -EPERM;
	}

	kfree(val);
	return ret;
}
static int iris_set_mode(void __user *argp)
{
	int ret;
	u32 mode, data;
	struct iris_mode_state *mode_state = &iris_info.mode_state;

	ret = copy_from_user(&data, argp, sizeof(u32));

	pr_info("iris2 iris_set_mode: new mode = %x, old  mode = %d\n",
		data, mode_state->sf_notify_mode);

	mode = data & 0xffff;
	if (mode != mode_state->sf_notify_mode)
	{
		mode_state->sf_notify_mode = mode;
		if (mode == IRIS_MODE_FRC_PREPARE)
			iris_video_fps_get(data >> 16);
		mode_state->sf_mode_switch_trigger = true;
	}
	return ret;
}

static int iris_get_mode(void __user *argp)
{
	int ret;
	uint32_t mode;
	struct iris_mode_state *mode_state = &iris_info.mode_state;

	mode = mode_state->sf_notify_mode;
	pr_debug("iris2 mode = %d\n", mode_state->sf_notify_mode);
	ret = copy_to_user(argp, &mode, sizeof(uint32_t));

	return ret;
}

u32 iris_hdr_enable_get(void)
{
	struct feature_setting *chip_setting = &iris_info.chip_setting;

	return chip_setting->hdr_setting.hdren;
}

int iris_ioctl_operate_mode(void __user *argp)
{
	int ret = -1;
	struct msmfb_iris_operate_value val;
	ret = copy_from_user(&val, argp, sizeof(val));
	if (ret != 0) {
		pr_err("iris2 can not copy from user\n");
		return -EPERM;
	}
	mutex_lock(&iris_info.config_mutex);
	if (val.type == IRIS_OPRT_MODE_SET) {
		ret = iris_set_mode(val.values);
	} else {
		ret = iris_get_mode(val.values);
	}
	mutex_unlock(&iris_info.config_mutex);

	return ret;
}

int iris_feature_setting_update_proc(void)
{
#if 1
	struct feature_setting *chip_setting = &iris_info.chip_setting;
	struct feature_setting *user_setting = &iris_info.user_setting;
	struct iris_setting_update *settint_update = &iris_info.settint_update;
	u32 lut_table_update = false, gamma_table_update = false;
	u32 cm3d_indx = 0;

	//mutex_lock(&iris_info.config_mutex);
        iris_frc_configs_update();

	// no update
	if (!settint_update->pq_setting && !settint_update->dbc_setting
		&& !settint_update->lp_memc_setting && !settint_update->color_adjust
		&& !settint_update->lce_setting &&!settint_update->cm_setting
		&& !settint_update->lux_value && !settint_update->cct_value
		&& !settint_update->reading_mode && !settint_update->hdr_setting
		&& !settint_update->gamma_table)
		return 0;

	
	// PQ setting, MB3
	if (settint_update->pq_setting) {
		chip_setting->pq_setting = user_setting->pq_setting;
		iris_cmd_reg_add(&meta_cmd, IRIS_PQ_SETTING_ADDR, *((u32 *)&chip_setting->pq_setting));
		pr_info("iris2 %s, %d: configValue = 0x%x.\n", __func__, __LINE__, *((u32 *)&chip_setting->pq_setting));
		settint_update->pq_setting = false;
	}

	// DBC setting, MB5
	if (settint_update->dbc_setting) {
		chip_setting->dbc_setting = user_setting->dbc_setting;
		iris_cmd_reg_add(&meta_cmd, IRIS_DBC_SETTING_ADDR, *((u32 *)&chip_setting->dbc_setting));
		settint_update->dbc_setting = false;
	}

	if (settint_update->lp_memc_setting) {
		chip_setting->lp_memc_setting = user_setting->lp_memc_setting;
		iris_cmd_reg_add(&meta_cmd, IRIS_LPMEMC_SETTING_ADDR, chip_setting->lp_memc_setting.value | 0x80000000);
		settint_update->lp_memc_setting = false;
	}

	if (settint_update->color_adjust) {
		chip_setting->color_adjust = user_setting->color_adjust;
		iris_cmd_reg_add(&meta_cmd, IRIS_COLOR_ADJUST_ADDR, (u32)chip_setting->color_adjust | 0x80000000);
		settint_update->color_adjust = false;
	}
	//LCE Setting,DSC_ENCODER_ALG_PARM2
	if (settint_update->lce_setting) {
		chip_setting->lce_setting = user_setting->lce_setting;
		iris_cmd_reg_add(&meta_cmd, IRIS_LCE_SETTING_ADDR, *((u32 *)&chip_setting->lce_setting));
		settint_update->lce_setting = false;
	}

	// CM Setting,DSC_ENCODER_ALG_PARM6
	if (settint_update->cm_setting) {
		if (chip_setting->cm_setting.cm3d != user_setting->cm_setting.cm3d) {
			cm3d_indx = user_setting->cm_setting.cm3d;
			lut_table_update = true;
			gamma_table_update = true;
		}
		chip_setting->cm_setting = user_setting->cm_setting;
		iris_cmd_reg_add(&meta_cmd, IRIS_CM_SETTING_ADDR, *((u32 *)&chip_setting->cm_setting));
		settint_update->cm_setting = false;
	}

	// HDR Setting
	if (settint_update->hdr_setting) {
		if (chip_setting->hdr_setting.hdren != user_setting->hdr_setting.hdren) {
			if (user_setting->hdr_setting.hdren) {
				cm3d_indx = CM_HDR;
			} else {
				cm3d_indx = chip_setting->cm_setting.cm3d;
				iris_cmd_reg_add(&meta_cmd, IRIS_CM_SETTING_ADDR, *((u32 *)&chip_setting->cm_setting));
			}
			lut_table_update = true;
			gamma_table_update = true;
			iris_datapath_csc_enable(user_setting->hdr_setting.hdren);
		}
		chip_setting->hdr_setting = user_setting->hdr_setting;
		iris_cmd_reg_add(&meta_cmd, IRIS_HDR_SETTING_ADDR, *((u32 *)&chip_setting->hdr_setting));
		settint_update->hdr_setting = false;
	}

	// Lux value
	if (settint_update->lux_value) {
		chip_setting->lux_value = user_setting->lux_value;
		iris_cmd_reg_add(&meta_cmd, IRIS_LUX_VALUE_ADDR, *((u32 *)&chip_setting->lux_value));
		pr_info("iris2 %s, %d: configValue = %d.\n", __func__, __LINE__, *((u32 *)&chip_setting->lux_value));
		settint_update->lux_value= false;
	}

	// cct value
	if (settint_update->cct_value) {
		chip_setting->cct_value = user_setting->cct_value;
		iris_cmd_reg_add(&meta_cmd, IRIS_CCT_VALUE_ADDR, *((u32 *)&chip_setting->cct_value));
		pr_info("iris2 %s, %d: configValue = %d.\n", __func__, __LINE__, *((u32 *)&chip_setting->cct_value));
		settint_update->cct_value= false;
	}

	// reading mode
	if (settint_update->reading_mode) {
		chip_setting->reading_mode = user_setting->reading_mode;
		iris_cmd_reg_add(&meta_cmd, IRIS_READING_MODE_ADDR, *((u32 *)&chip_setting->reading_mode));
		pr_info("iris2 %s, %d: configValue = %d.\n", __func__, __LINE__, *((u32 *)&chip_setting->reading_mode));
		settint_update->reading_mode= false;
	}

	if (settint_update->gamma_table) {
		chip_setting->gamma_enable = user_setting->gamma_enable;
		gamma_table_update = true;
		settint_update->gamma_table = false;
	}
	if (settint_update->demo_win_fi
		|| settint_update->demo_win_peaking
		|| settint_update->demo_win_cm) {
		iris_demo_window_set();
	}

	if (lut_table_update) {
		iris_cmlut_table_update(iris_panel, cm3d_indx);
	}
	if (gamma_table_update) {
		iris_gamma_table_update(chip_setting);
	}

	iris_cmd_reg_add(&meta_cmd, IRIS_PROXY_MB0_ADDR, PQ_UPDATING_FLAG);
	//mutex_unlock(&iris_info.config_mutex);
	return 0;
#endif
}


int iris_ioctl_operate_conf(void __user *argp)
{
	int ret = -1;
	uint32_t parent_type = 0;
	uint32_t child_type = 0;
	struct msmfb_iris_operate_value configure;

	ret = copy_from_user(&configure, argp, sizeof(configure));
	if (ret)
		return ret;

	pr_debug("iris2 %s type = %d, value = %d\n",
				__func__, configure.type, configure.count);

	child_type = (configure.type >> 8) & 0xff;
	parent_type = configure.type & 0xff;

	mutex_lock(&iris_info.config_mutex);
	switch (parent_type) {
		case IRIS_OPRT_ROTATION_SET:
			ret = iris_oprt_rotation_set(configure.values);
			break;
                case IRIS_OPRT_VIDEO_FRAME_RATE_SET:
                        ret = iris_oprt_video_framerate_set(configure.values);
                        break;
		case IRIS_OPRT_CONFIGURE:
			ret = iris_oprt_configure(child_type, configure.values);
			break;
		case IRIS_OPRT_CONFIGURE_NEW:
			ret = iris_oprt_configure_ex(child_type,
						configure.count, configure.values);
			break;
		case IRIS_OPRT_CONFIGURE_NEW_GET:
			ret = iris_oprt_configure_get(child_type,
							configure.count, configure.values);
			break;
		case IRIS_OPRT_GET_LCD_CALIBRATE_DATA:
			ret = iris_oprt_get_lcd_calib_data(child_type,
							configure.count, configure.values);
			break;
		default:
			pr_err("iris2 could not find right opertat type = %d\n", configure.type);
			break;
	}
	mutex_unlock(&iris_info.config_mutex);
	return ret;
}

bool iris_appcode_ready_wait(void)
{
	if (iris_info.firmware_info.app_fw_state)
		iris_info.firmware_info.app_cnt++;
	else
		return false;

	/* check appcode init done */
	iris_appcode_init_done_wait(iris_panel);

	if (iris_info.firmware_info.app_cnt > 10)
		return true;
	else
		return false;

}

int iris_low_power_process(void)
{
	u8 signal_mode = 0xff, mode_switch, lp_switch, cm_switch;
	int *state = &iris_info.power_status.low_power_state;
	struct iris_mode_state *mode_state = &iris_info.mode_state;
	struct feature_setting *chip_setting = &iris_info.chip_setting;
	struct feature_setting *user_setting = &iris_info.user_setting;
	struct iris_setting_update *settint_update = &iris_info.settint_update;
	u32 val;

	if(iris_debug_power_opt_disable || (LP_STATE_POWER_UP == *state))
		return true;

	lp_switch = (LP_STATE_POWER_UP_PREPARE == *state)
				|| (LP_STATE_POWER_DOWN_PREPARE == *state);

	mode_switch = mode_state->sf_mode_switch_trigger
		&& ((IRIS_MODE_FRC_PREPARE == mode_state->sf_notify_mode)
			|| (IRIS_MODE_RFB_PREPARE == mode_state->sf_notify_mode));

	cm_switch = settint_update->hdr_setting
				|| settint_update->gamma_table
				|| (settint_update->cm_setting
					&& (chip_setting->cm_setting.cm3d != user_setting->cm_setting.cm3d));

	if (lp_switch || mode_switch || cm_switch) {
		iris_i2c_reg_read(IRIS_MIPI_RX_ADDR + DCS_CMD_PARA_2, &val);
		signal_mode = val & 0xff;
		if (signal_mode)
			return false;
		if (LP_STATE_POWER_UP_PREPARE == *state) {
			*state = LP_STATE_POWER_UP;
			return true;
		} else if (LP_STATE_POWER_DOWN_PREPARE == *state) {
			*state = LP_STATE_POWER_DOWN;
		}
		if ((mode_switch || cm_switch) && (LP_STATE_POWER_DOWN == *state)) {
			iris_i2c_reg_write(IRIS_MIPI_RX_ADDR + DCS_CMD_PARA_2, LP_CMD_LP_EXIT);
			*state = LP_STATE_POWER_UP_PREPARE;
			return false;
		}

		pr_info("iris2 lp state = %d\n", *state);
	}

	return true;
}

void iris_low_power_mode_notify(void)
{
	int *state = &iris_info.power_status.low_power_state;
	struct iris_mode_state *mode_state = &iris_info.mode_state;

	if (iris_debug_power_opt_disable || (LP_STATE_POWER_DOWN == *state))
		return;

	if (!(((IRIS_RFB_MODE == mode_state->current_mode) && (IRIS_MODE_RFB == mode_state->sf_notify_mode))
		|| ((IRIS_FRC_MODE == mode_state->current_mode) && (IRIS_MODE_FRC == mode_state->sf_notify_mode))))
		return;

	iris_i2c_reg_write(IRIS_MIPI_RX_ADDR + DCS_CMD_PARA_2, LP_CMD_LP_ENTER);
	*state = LP_STATE_POWER_DOWN_PREPARE;
	pr_info("iris2 enter low power mode\n");
}

void iris_cmd_kickoff_proc(void)
{
	u32 grcp_len = 0;
	static bool first_boot = true;
	struct dsi_cmd_desc iris_meta_cmd = {
		{0, MIPI_DSI_GENERIC_LONG_WRITE, 0, 0, 0, CMD_PKT_SIZE, meta_cmd.cmd, 1, iris_read_cmd_buf}, 1, 0};

	if ((false == iris_appcode_ready_wait()) || (!iris_panel->panel_initialized))
		return;

	//pr_err("iris2 kickoff\n");
	if (first_boot) {
		memset(&meta_cmd, 0, sizeof(meta_cmd));
		memcpy(meta_cmd.cmd, grcp_header, GRCP_HEADER);
		first_boot = false;
	}
	meta_cmd.cmd_len = GRCP_HEADER;

	mutex_lock(&iris_info.config_mutex);

	/* update cm setting until cm/gamma table ready */
	if (iris_cm_setting_force_update) {
		if (iris_info.lut_info.lut_fw_state && iris_info.gamma_info.gamma_fw_state) {
			iris_info.settint_update.cm_setting = true;
			iris_cm_setting_force_update = false;
		}
	}

	if (iris_low_power_process()) {
		iris_feature_setting_update_proc();
		iris_mode_switch_cmd(iris_panel);
		iris_low_power_mode_notify();
	}
	mutex_unlock(&iris_info.config_mutex);

	if (meta_cmd.cmd_len > GRCP_HEADER) {
		grcp_len = (meta_cmd.cmd_len - GRCP_HEADER) / 4;
		*(u32 *)(meta_cmd.cmd + 8) = cpu_to_le32(grcp_len + 1);
		*(u16 *)(meta_cmd.cmd + 14) = cpu_to_le16(grcp_len);
		
		iris_meta_cmd.msg.tx_len = meta_cmd.cmd_len;
		iris_dsi_cmds_send(iris_panel, &iris_meta_cmd, 1, DSI_CMD_SET_STATE_HS);
	}	
}
