
/*
 * Important:
 *
 * This file using "UTF-8 65001 with BOM" coding.
 * Choose editor coding if you could not see words below.
 *
 * “防控烦人的乱码，编辑器使用上述编码才看得到这句话！”
 * “这句就是故意给编辑器看的，它一般会识别头部，勿删。”
 */

#ifndef ZMV_CONFIG_HANDLER_210220_H
#define ZMV_CONFIG_HANDLER_210220_H

#ifdef __cplusplus
extern "C" {
#endif

#define LOAD_INIT_CONFIG                                            0
#define LOAD_CUSTOM_CONFIG                                          1

typedef enum
{
	e_device_softe_version,
    e_device_input_type,
    e_device_eth0,
    e_device_eth1,
    e_device_upward_can_id,
    e_device_track_frame_delay,
    e_device_tcp_ctl_port,
	e_device_fps,
	e_device_track_search_time,
    e_device_bitrate,
    e_rtsp_client_server_ip,
    e_rtsp_client_port,
    e_rtsp_client_name,
    e_rtsp_client_user_name,
    e_rtsp_client_password,
    e_rtsp_client_stream_type,
    e_rtsp_client_protocol,
    e_rtsp_server_state,
    e_rtsp_server_port,
    e_rtsp_server_stream_type,
    e_ts_send_state,
    e_ts_send_stream_type,
    e_ts_send_ip,
    e_ts_send_port,
    e_ts_recv_input_device,
    e_ts_recv_stream_type,
    e_ts_recv_port,
	e_track_param_lost_threshs0,
	e_track_param_lost_threshs1,
	e_track_param_lost_threshs2,
	e_track_param_lost_threshs3,
	e_track_param_min_hist_diff,
	e_track_param_do_throw_away,
	e_track_param_max_obj_lost_num,
	e_track_param_find_obj_threshold,
	e_track_param_server_movement_factor,
	e_track_param_detect_scale_factor,
	e_track_param_refind_threshold,
	e_track_param_max_score_threshold,
	e_track_param_contrast_threshold,
	e_track_param_eco_search_area_scale,
	e_track_param_learning_rate,
	e_track_param_scale_step,
	e_track_param_use_obj_detect_to_refind,
	e_track_param_use_obj_detect_only,
	e_track_param_use_forecast_pushing,

	e_track_param_use_distance_punishment,//new
	e_track_param_obj_refind_delay,
	e_track_param_use_shelter_modules,

	e_track_param_deny_when_low_contrast,
	e_track_param_use_track_when_multi_obj,
	e_track_param_print_info,
    e_member_max
}
config_member_et;

int
zmv_config_file_load(const char *file_name, int method);

const char *
zmv_config_get_value(config_member_et e);

void
zmv_config_set_value(config_member_et e, const char *new_val);

void
zmv_config_reset_to_default_value(config_member_et e);

int
zmv_config_file_generate(const char *file_name);

void
zmv_config_get_value_string(config_member_et e,char* value,int size);

int
zmv_config_get_value_int(config_member_et e);

float
zmv_config_get_value_float(config_member_et e);

#ifdef __cplusplus
}
#endif

#endif // ZMV_CONFIG_HANDLER_210220_H
