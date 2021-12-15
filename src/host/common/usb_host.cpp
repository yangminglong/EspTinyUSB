#include "esp32-hal-log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "usb/usb_host.h"

#include "usb_host.hpp"

void _client_event_callback(const usb_host_client_event_msg_t *event_msg, void *arg)
{
    USBhost *host = (USBhost *)arg;
    if (event_msg->event == USB_HOST_CLIENT_EVENT_NEW_DEV)
    {
        log_i("USB_HOST_CLIENT_EVENT_NEW_DEV client event: %d, address: %d\n", event_msg->event, event_msg->new_dev.address);
        if (host->_client_event_cb)
        {
            host->_client_event_cb(event_msg, arg);
        } else {
            host->open(event_msg);
        }
    } else {
        log_i("USB_HOST_CLIENT_EVENT_DEV_GONE client event: %d\n", event_msg->event);
        if (host->_client_event_cb)
        {
            host->_client_event_cb(event_msg, arg);
            host->close();
        }
    }
}

void client_async_seq_task(void *param)
{
    USBhost* host = (USBhost *)param;
    log_i("create async task\n");
    while (1)
    {
        usb_host_client_handle_t client_hdl = host->client_hdl;
        uint32_t event_flags;
        if(client_hdl) usb_host_client_handle_events(client_hdl, 1);

        if (ESP_OK == usb_host_lib_handle_events(0, &event_flags))
        {
            if (event_flags & USB_HOST_LIB_EVENT_FLAGS_NO_CLIENTS)
            {
                log_i("No more clients\n");
                do{
                    if(usb_host_device_free_all() != ESP_ERR_NOT_FINISHED) break;
                }while(1);

                log_i("usb_host_uninstall and host->init( not create_tasks)\n");

                esp_err_t err = usb_host_uninstall();
                if (err)
                    log_i("usb_host_uninstall status: %X\n", err);
                
                host->init(false);
            }
            if (event_flags & USB_HOST_LIB_EVENT_FLAGS_ALL_FREE)
            {
                log_i("USB_HOST_LIB_EVENT_FLAGS_ALL_FREE\n");
                esp_err_t err = usb_host_client_deregister(client_hdl);
                if (err)
                    log_i("usb_host_client_deregister status: %X\n", err);
                host->client_hdl = NULL;                
            }
        } else {
            vTaskDelay(1);
        }
    }
    log_i("delete task\n");
    vTaskDelete(NULL);
}

USBhost::USBhost()
{
}

USBhost::~USBhost()
{

}


bool USBhost::init(bool create_tasks)
{
    const usb_host_config_t config = {
        .intr_flags = ESP_INTR_FLAG_LEVEL1,
    };

    log_i("usb_host_install\n");
    esp_err_t err = usb_host_install(&config);
    if (err)
        log_i("install status: %X\n", err);

    const usb_host_client_config_t client_config = {
        .is_synchronous = false,
        .max_num_event_msg = 5,
        .async = {
            .client_event_callback = _client_event_callback,
            .callback_arg = this,
        }
    };

    log_i("usb_host_client_register\n");
    err = usb_host_client_register(&client_config, &client_hdl);
    if (err)
        log_i("client register status: %X\n", err);

    if (create_tasks)
    {
        TaskHandle_t xHandle = NULL;
        xTaskCreate(client_async_seq_task, "async", 6 * 512, this, 5|portPRIVILEGE_BIT , &xHandle);

        configASSERT( xHandle );

        log_i("xTaskCreate started. xHandle: %d\n", xHandle);
    }

    return true;
}

bool USBhost::open(const usb_host_client_event_msg_t *event_msg)
{
    esp_err_t err = usb_host_device_open(client_hdl, event_msg->new_dev.address, &dev_hdl);
    if (err)
        log_i("usb_host_device_open status: %X\n", err);
    parseConfig();

    return true;
}

void USBhost::close()
{
    // usb_host_interface_release(client_hdl, dev_hdl, MOCK_MSC_SCSI_INTF_NUMBER);

    log_i("usb_host_device_close\n");
    esp_err_t err = usb_host_device_close(client_hdl, dev_hdl);
    if (err)
        log_i("usb_host_device_close status: %X\n", err);
}

void USBhost::parseConfig()
{
    const usb_device_desc_t *device_desc;
    esp_err_t err = usb_host_get_device_descriptor(dev_hdl, &device_desc);
    if (err)
        log_i("usb_host_get_device_descriptor status: %X\n", err);
    // ESP_LOG_BUFFER_HEX("", device_desc->val, USB_DEVICE_DESC_SIZE);
    const usb_config_desc_t *config_desc;
    err = usb_host_get_active_config_descriptor(dev_hdl, &config_desc);
    if (err)
        log_i("usb_host_get_active_config_descriptor status: %X\n", err);
}

usb_device_info_t USBhost::getDeviceInfo()
{
    esp_err_t err = usb_host_device_info(dev_hdl, &dev_info);
    if (err)
        log_i("usb_host_device_info status: %X\n", err);

    return dev_info;
}

const usb_device_desc_t* USBhost::getDeviceDescriptor()
{
    const usb_device_desc_t *device_desc;
    esp_err_t err = usb_host_get_device_descriptor(dev_hdl, &device_desc);
    if (err)
        log_i("usb_host_get_device_descriptor status: %X\n", err);

    return device_desc;
}

const usb_config_desc_t* USBhost::getConfigurationDescriptor()
{
    const usb_config_desc_t *config_desc;
    esp_err_t err = usb_host_get_active_config_descriptor(dev_hdl, &config_desc);
    if (err)
        log_i("usb_host_get_device_descriptor status: %X\n", err);
    return config_desc;
}

uint8_t USBhost::getConfiguration()
{
    return getDeviceInfo().bConfigurationValue;
}

usb_host_client_handle_t USBhost::clientHandle()
{
    return client_hdl;
}

usb_device_handle_t USBhost::deviceHandle()
{
    return dev_hdl;
}

// bool USBhost::setConfiguration(uint8_t);
