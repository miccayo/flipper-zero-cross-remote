#include "../xremote.h"
#include "../views/xremote_transmit.h"
#include "../models/infrared/xremote_ir_signal.h"
#include "../helpers/subghz/subghz.h"

static const NotificationSequence* xremote_notification_sequences[] = {
    &sequence_success,
    &sequence_set_only_green_255,
    &sequence_reset_green,
    &sequence_solid_yellow,
    &sequence_reset_rgb,
    &sequence_blink_start_cyan,
    &sequence_blink_start_magenta,
    &sequence_blink_stop,
    &sequence_blink_start_yellow,
    &sequence_blink_stop,
    &sequence_blink_start_blue,
    &sequence_blink_stop,
};

void xremote_transmit_callback(XRemoteCustomEvent event, void* context) {
    furi_assert(context);
    XRemote* app = context;
    view_dispatcher_send_custom_event(app->view_dispatcher, event);
}

void xremote_scene_ir_notification_message(XRemote* app, uint32_t message) {
    if(app->led == 1) {
        notification_message(app->notification, xremote_notification_sequences[message]);
    }
}

bool xremote_scene_ir_signal_is_raw(InfraredSignal* signal) {
    if(signal->is_raw) {
        return true;
    }
    return false;
}

void xremote_scene_transmit_stop_ir_signal(XRemote* app) {
    if(!app->transmitting) {
        return;
    }
    app->transmitting = false;
    infrared_worker_tx_stop(app->ir_worker);
    infrared_worker_tx_set_get_signal_callback(app->ir_worker, NULL, NULL);
    xremote_scene_ir_notification_message(app, InfraredNotificationMessageBlinkStop);
}

void xremote_scene_transmit_send_ir_signal(XRemote* app, CrossRemoteItem* item) {
    InfraredSignal* signal = xremote_cross_remote_item_get_ir_signal(item);
    dolphin_deed(DolphinDeedIrSend);
    xremote_scene_ir_notification_message(app, InfraredNotificationMessageBlinkStartSend);
    if(xremote_scene_ir_signal_is_raw(signal)) {
        InfraredRawSignal* raw = xremote_ir_signal_get_raw_signal(signal);
        infrared_worker_set_raw_signal(
            app->ir_worker, raw->timings, raw->timings_size, raw->frequency, raw->duty_cycle);
    } else {
        InfraredMessage* message = xremote_ir_signal_get_message(signal);
        infrared_worker_set_decoded_signal(app->ir_worker, message);
    }
    infrared_worker_tx_set_get_signal_callback(
        app->ir_worker, infrared_worker_tx_get_signal_steady_callback, app);
    infrared_worker_tx_start(app->ir_worker);
    app->transmitting = true;
    uint32_t time = app->ir_timing;
    if(item->time > 0) {
        time = item->time;
    }
    furi_thread_flags_wait(0, FuriFlagWaitAny, time);
    xremote_scene_transmit_stop_ir_signal(app);
}

void xremote_scene_transmit_send_pause(XRemote* app, CrossRemoteItem* item) {
    app->transmitting = true;
    xremote_scene_ir_notification_message(app, PauseNotificationMessageBlinkStartSend);
    furi_thread_flags_wait(0, FuriFlagWaitAny, item->time * 1000);
    app->transmitting = false;
    xremote_scene_ir_notification_message(app, PauseNotificationMessageBlinkStop);
}

void xremote_scene_transmit_send_subghz(XRemote* app, CrossRemoteItem* item) {
    app->transmitting = true;
    xremote_scene_ir_notification_message(app, SubGhzNotificationMessageBlinkStartSend);
    if(furi_string_utf8_length(item->filename) < 3) {
        xremote_cross_remote_set_transmitting(app->cross_remote, XRemoteTransmittingStop);
        app->transmitting = false;
        return;
    }
    subghz_send(app, furi_string_get_cstr(item->filename));
    //furi_thread_flags_wait(0, FuriFlagWaitAny, 2000);
}

void xremote_scene_transmit_send_signal(void* context, CrossRemoteItem* item) {
    furi_assert(context);
    XRemote* app = context;
    CrossRemote* remote = app->cross_remote;

    if(app->transmitting) {
        return;
    }

    xremote_transmit_model_set_name(
        app->xremote_transmit, xremote_cross_remote_item_get_name(item));
    xremote_transmit_model_set_type(app->xremote_transmit, item->type);
    if(item->type == XRemoteRemoteItemTypeInfrared) {
        xremote_scene_transmit_send_ir_signal(app, item);
        xremote_cross_remote_set_transmitting(remote, XRemoteTransmittingStop);
    } else if(item->type == XRemoteRemoteItemTypePause) {
        xremote_scene_transmit_send_pause(app, item);
        xremote_cross_remote_set_transmitting(remote, XRemoteTransmittingStop);
    } else if(item->type == XRemoteRemoteItemTypeSubGhz) {
        xremote_scene_transmit_send_subghz(app, item);
    }
}

void xremote_scene_transmit_run_remote(void* context) {
    furi_assert(context);
    XRemote* app = context;
    CrossRemote* remote = app->cross_remote;

    size_t item_count = xremote_cross_remote_get_item_count(remote);
    if (app->loop_transmit) {
        while (true) {
            for (size_t i = 0; i < item_count;) {
                if (xremote_cross_remote_get_transmitting(remote) == XRemoteTransmittingIdle) {
                    xremote_cross_remote_set_transmitting(remote, XRemoteTransmittingStart);
                    CrossRemoteItem* item = xremote_cross_remote_get_item(remote, i);
                    xremote_scene_transmit_send_signal(app, item);
                } else if (xremote_cross_remote_get_transmitting(remote) == XRemoteTransmittingStopSubghz) {
                    i++;
                    app->state_notifications = SubGhzNotificationStateIDLE;
                    app->transmitting = false;
                    subghz_txrx_stop(app->subghz->txrx);
                    xremote_scene_ir_notification_message(app, SubGhzNotificationMessageBlinkStop);
                    xremote_cross_remote_set_transmitting(remote, XRemoteTransmittingIdle);
                } else if (xremote_cross_remote_get_transmitting(remote) == XRemoteTransmittingStop) {
                    i++;
                    xremote_cross_remote_set_transmitting(remote, XRemoteTransmittingIdle);
                }
            }
        }
    } else {
        for (size_t i = 0; i < item_count;) {
                if (xremote_cross_remote_get_transmitting(remote) == XRemoteTransmittingIdle) {
                    xremote_cross_remote_set_transmitting(remote, XRemoteTransmittingStart);
                    CrossRemoteItem* item = xremote_cross_remote_get_item(remote, i);
                    xremote_scene_transmit_send_signal(app, item);
                } else if (xremote_cross_remote_get_transmitting(remote) == XRemoteTransmittingStopSubghz) {
                    i++;
                    app->state_notifications = SubGhzNotificationStateIDLE;
                    app->transmitting = false;
                    subghz_txrx_stop(app->subghz->txrx);
                    xremote_scene_ir_notification_message(app, SubGhzNotificationMessageBlinkStop);
                    xremote_cross_remote_set_transmitting(remote, XRemoteTransmittingIdle);
                } else if (xremote_cross_remote_get_transmitting(remote) == XRemoteTransmittingStop) {
                    i++;
                    xremote_cross_remote_set_transmitting(remote, XRemoteTransmittingIdle);
                }
            }
    }
    xremote_scene_ir_notification_message(app, InfraredNotificationMessageBlinkStop);

    scene_manager_previous_scene(app->scene_manager);
}

void xremote_scene_transmit_on_enter(void* context) {
    furi_assert(context);
    XRemote* app = context;
    xremote_transmit_set_callback(app->xremote_transmit, xremote_transmit_callback, app);

    view_dispatcher_switch_to_view(app->view_dispatcher, XRemoteViewIdTransmit);
    xremote_scene_transmit_run_remote(app);
}

bool xremote_scene_transmit_on_event(void* context, SceneManagerEvent event) {
    XRemote* app = context;
    bool consumed = false;

    if(event.type == SceneManagerEventTypeCustom) {
        FURI_LOG_D(TAG, "Custom Event");
        switch(event.event) {
        case XRemoteCustomEventViewTransmitterSendStop:
            FURI_LOG_D("SUBGHZ", "stop event"); // doesn't trigger
            break;
        default:
            break;
        }
    } else if(event.type == SceneManagerEventTypeTick) {
        FURI_LOG_D(TAG, "Tick Event");
        with_view_model(
            xremote_transmit_get_view(app->xremote_transmit),
            void* model,
            { UNUSED(model); },
            true);
        if(app->state_notifications == SubGhzNotificationStateTx && app->led == 1) {
            //blink for subghz
        }
    }

    return consumed;
}

void xremote_scene_transmit_on_exit(void* context) {
    XRemote* app = context;
    app->transmitting = false;
    app->state_notifications = SubGhzNotificationStateIDLE;
}