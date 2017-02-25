ccflags-y += -I$(src)

obj-$(CONFIG_FB_MSM_MDSS_MHL3) += mhl3/

mdss-mdp3-objs = mdp3.o mdp3_dma.o mdp3_ctrl.o dsi_status_v2.o
mdss-mdp3-objs += mdp3_ppp.o mdp3_ppp_hwio.o mdp3_ppp_data.o
obj-$(CONFIG_FB_MSM_MDSS_MDP3) += mdss-mdp3.o

mdss-mdp-objs := mdss_mdp.o mdss_mdp_ctl.o mdss_mdp_pipe.o mdss_mdp_util.o dsi_status_6g.o
mdss-mdp-objs += mdss_mdp_pp.o mdss_mdp_pp_debug.o mdss_mdp_pp_cache_config.o
mdss-mdp-objs += mdss_mdp_intf_video.o
mdss-mdp-objs += mdss_mdp_intf_cmd.o
mdss-mdp-objs += mdss_mdp_intf_writeback.o
mdss-mdp-objs += mdss_mdp_rotator.o
mdss-mdp-objs += mdss_rotator.o
mdss-mdp-objs += mdss_mdp_overlay.o
mdss-mdp-objs += mdss_mdp_layer.o
mdss-mdp-objs += mdss_mdp_splash_logo.o
mdss-mdp-objs += mdss_mdp_cdm.o
mdss-mdp-objs += mdss_smmu.o
mdss-mdp-objs += mdss_mdp_wfd.o
obj-$(CONFIG_FB_MSM_MDSS) += mdss-mdp.o
obj-$(CONFIG_FB_MSM_MDSS) += mdss_mdp_debug.o

ifeq ($(CONFIG_ARCH_MSM8996),y)
mdss-mdp-objs += mdss_mdp_pp_v1_7.o
endif

ifeq ($(CONFIG_ARCH_MSM8937),y)
mdss-mdp-objs += mdss_mdp_pp_v1_7.o
endif

ifeq ($(CONFIG_FB_MSM_MDSS),y)
obj-$(CONFIG_DEBUG_FS) += mdss_debug.o mdss_debug_xlog.o
endif

mdss-dsi-objs := mdss_dsi.o mdss_dsi_host.o mdss_dsi_cmd.o mdss_dsi_status.o
mdss-dsi-objs += mdss_dsi_panel.o
ifeq ($(CONFIG_LGE_DISPLAY_READER_MODE),y)
mdss-dsi-objs += lge/reader_mode.o
endif
ifeq ($(CONFIG_LGE_DISPLAY_COMMON),y)
mdss-dsi-objs += lge/lge_mdss_dsi.o
mdss-dsi-objs += lge/lge_mdss_dsi_panel.o
endif
ifeq ($(CONFIG_LGE_DISPLAY_PH2_GLOBAL_COM),y)
mdss-dsi-objs += lge_ph2_global_com/lge_mdss_dsi_ph2_global_com.o
mdss-dsi-objs += lge_ph2_global_com/lge_mdss_dsi_panel_ph2_global_com.o
endif
ifeq ($(CONFIG_LGE_DISPLAY_PH2_CMO_CN),y)
mdss-dsi-objs += lge_ph2_cmo_cn/lge_mdss_dsi_ph2_cmo_cn.o
mdss-dsi-objs += lge_ph2_cmo_cn/lge_mdss_dsi_panel_ph2_cmo_cn.o
endif
ifeq ($(CONFIG_LGE_DISPLAY_PH2N_TMO_US),y)
mdss-dsi-objs += lge_ph2n_tmo_us/lge_mdss_dsi_ph2n_tmo_us.o
mdss-dsi-objs += lge_ph2n_tmo_us/lge_mdss_dsi_panel_ph2n_tmo_us.o
endif
ifeq ($(CONFIG_LGE_DISPLAY_PH2N_MPCS_US),y)
mdss-dsi-objs += lge_ph2n_tmo_us/lge_mdss_dsi_ph2n_mpcs_us.o
mdss-dsi-objs += lge_ph2n_tmo_us/lge_mdss_dsi_panel_ph2n_mpcs_us.o
endif
ifeq ($(CONFIG_LGE_DISPLAY_PH2N_GLOBAL_CA),y)
mdss-dsi-objs += lge_ph2n_global_ca/lge_mdss_dsi_ph2n_global_ca.o
mdss-dsi-objs += lge_ph2n_global_ca/lge_mdss_dsi_panel_ph2n_global_ca.o
endif
ifeq ($(CONFIG_LGE_DISPLAY_L5_DCM_JP),y)
mdss-dsi-objs += lge_k7j/lge_mdss_dsi_k7j.o
mdss-dsi-objs += lge_k7j/lge_mdss_dsi_panel_k7j.o
endif
ifeq ($(CONFIG_LGE_DISPLAY_JSG_KDDI_JP),y)
mdss-dsi-objs += lge_k7j/lge_mdss_dsi_k7j.o
mdss-dsi-objs += lge_k7j/lge_mdss_dsi_panel_k7j.o
endif
ifeq ($(CONFIG_LGE_DISPLAY_MFTS),y)
mdss-dsi-objs += lge/mfts_mode.o
endif
mdss-dsi-objs += msm_mdss_io_8974.o
mdss-dsi-objs += mdss_dsi_phy.o
mdss-dsi-objs += mdss_dsi_clk.o
obj-$(CONFIG_FB_MSM_MDSS) += mdss-dsi.o
obj-$(CONFIG_FB_MSM_MDSS) += mdss_panel.o
obj-$(CONFIG_FB_MSM_MDSS) += mdss_hdmi_util.o
obj-$(CONFIG_FB_MSM_MDSS) += mdss_hdmi_edid.o
obj-$(CONFIG_FB_MSM_MDSS) += mdss_cec_core.o
obj-$(CONFIG_FB_MSM_MDSS) += mdss_dba_utils.o
obj-$(CONFIG_FB_MSM_MDSS_EDP_PANEL) += mdss_edp.o
obj-$(CONFIG_FB_MSM_MDSS_EDP_PANEL) += mdss_edp_aux.o

obj-$(CONFIG_FB_MSM_MDSS) += mdss_io_util.o
obj-$(CONFIG_FB_MSM_MDSS_HDMI_PANEL) += mdss_hdmi_tx.o
obj-$(CONFIG_FB_MSM_MDSS_HDMI_PANEL) += mdss_hdmi_hdcp.o
obj-$(CONFIG_FB_MSM_MDSS_HDMI_PANEL) += mdss_hdmi_hdcp2p2.o
obj-$(CONFIG_FB_MSM_MDSS_HDMI_PANEL) += mdss_hdmi_cec.o
obj-$(CONFIG_FB_MSM_MDSS_HDMI_MHL_SII8334) += mhl_sii8334.o mhl_msc.o

obj-$(CONFIG_FB_MSM_MDSS_WRITEBACK) += mdss_wb.o

mdss-qpic-objs := mdss_qpic.o mdss_fb.o mdss_qpic_panel.o
obj-$(CONFIG_FB_MSM_QPIC) += mdss-qpic.o
obj-$(CONFIG_FB_MSM_QPIC_ILI_QVGA_PANEL) += qpic_panel_ili_qvga.o

obj-$(CONFIG_FB_MSM_MDSS) += mdss_fb.o mdss_util.o
obj-$(CONFIG_COMPAT) += mdss_compat_utils.o
