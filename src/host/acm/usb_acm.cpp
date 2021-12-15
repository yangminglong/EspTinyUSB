#include "esp_log.h"
#include "string.h"

#include "usb_acm.hpp"
#include "esp32-hal-log.h"

void IRAM_ATTR usb_ctrl_cb(usb_transfer_t *transfer)
{
    USBacmDevice *dev = (USBacmDevice *)transfer->context;

    if (transfer->data_buffer[0] == SET_VALUE && transfer->data_buffer[1] == SET_LINE_CODING) // set line coding
    {
        dev->_callback(CDC_CTRL_SET_LINE_CODING, transfer);
    }
    else if (transfer->data_buffer[0] == GET_VALUE && transfer->data_buffer[1] == GET_LINE_CODING) // get line coding
    {
        dev->_callback(CDC_CTRL_GET_LINE_CODING, transfer);
    }
    else if (transfer->data_buffer[0] == SET_VALUE && transfer->data_buffer[1] == SET_CONTROL_LINE_STATE) // set line coding
    {
        dev->_callback(CDC_CTRL_SET_CONTROL_LINE_STATE, transfer);
    }
}

void IRAM_ATTR usb_read_cb(usb_transfer_t *transfer)
{
    USBacmDevice *dev = (USBacmDevice *)transfer->context;
    dev->_callback(CDC_DATA_IN, transfer);
}

void IRAM_ATTR usb_write_cb(usb_transfer_t *transfer)
{
    USBacmDevice *dev = (USBacmDevice *)transfer->context;
    dev->_callback(CDC_DATA_OUT, transfer);
}

USBacmDevice::USBacmDevice(const usb_config_desc_t *config_desc, USBhost *host)
{
    _host = host;
    bNumInterfaces = config_desc->bNumInterfaces;
    int offset_intf = 0;
    for (size_t n = 0; n < config_desc->bNumInterfaces; n++)
    {
        const usb_intf_desc_t *intf = usb_parse_interface_descriptor(config_desc, n, 0, &offset_intf);
        const usb_ep_desc_t *ep  = nullptr;

        int offset_ep = offset_intf;
        if (intf->bInterfaceClass == 0x02 )
        {
            if (intf->bNumEndpoints != 1)
                return;
            offset_ep = offset_intf;
            ep = usb_parse_endpoint_descriptor_by_index(intf, 0, config_desc->wTotalLength, &offset_ep);
            ep_int = ep;
            log_i("EP CDC comm.");

            log_i("EP num: %d/%d, len: %d, ", 1, intf->bNumEndpoints, config_desc->wTotalLength);
            if (ep == NULL) 
                log_w("-----ep == NULL");
            else if (ep)
                log_i("address: 0x%02x, EP max size: %d, dir: %s\n", ep->bEndpointAddress, ep->wMaxPacketSize, (ep->bEndpointAddress & 0x80) ? "IN" : "OUT");
            else
                log_w("-----error to parse endpoint by index; EP num: %d/%d, len: %d", 1, intf->bNumEndpoints, config_desc->wTotalLength);

            esp_err_t err = usb_host_interface_claim(_host->clientHandle(), _host->deviceHandle(), n, 0);
            log_i("interface claim status: %02X", err);
            itf_num = 0;
        }
        else if (intf->bInterfaceClass == 0x0a || intf->bInterfaceClass == 0xFF)
        {
            if (intf->bNumEndpoints != 2) {
                log_w("-----intf->bNumEndpoints != 2. : %d", intf->bNumEndpoints);
                return;
            }
            log_i("EP CDC data.");
            for (size_t i = 0; i < intf->bNumEndpoints; i++)
            {
                offset_ep = offset_intf;
                ep = usb_parse_endpoint_descriptor_by_index(intf, i, config_desc->wTotalLength, &offset_ep);
                if (ep == NULL) {
                    log_w("-----ep == NULL");
                }
                else if (ep->bEndpointAddress & 0x80)
                {
                    ep_in = ep;
                }
                else
                {
                    ep_out = ep;
                }

                // esp_err_t err = usb_host_endpoint_halt(_host->deviceHandle(), ep->bEndpointAddress);
                // if (err)
                //     log_w("-----usb_host_endpoint_halt status: %02X", err);
                // err = usb_host_endpoint_clear(_host->deviceHandle(), ep->bEndpointAddress);
                // if (err)
                //     log_w("-----usb_host_endpoint_clear status: %02X", err);


                log_i("EP num: %d/%d, len: %d, ", i + 1, intf->bNumEndpoints, config_desc->wTotalLength);
                if (ep)
                    log_i("address: 0x%02x, EP max size: %d, dir: %s\n", ep->bEndpointAddress, ep->wMaxPacketSize, (ep->bEndpointAddress & 0x80) ? "IN" : "OUT");
                else
                    log_w("-----error to parse endpoint by index; EP num: %d/%d, len: %d", i + 1, intf->bNumEndpoints, config_desc->wTotalLength);
            }
            esp_err_t err = usb_host_interface_claim(_host->clientHandle(), _host->deviceHandle(), n, 0);
            if (err)
                log_w("-----usb_host_interface_claim status: %02X", err);
            // for (size_t i = 0; i < intf->bNumEndpoints; i++) {
            //     offset_ep = offset_intf;
            //     ep = usb_parse_endpoint_descriptor_by_index(intf, i, config_desc->wTotalLength, &offset_ep);
            //     usb_host_endpoint_halt(_host->deviceHandle(), ep->bEndpointAddress);
            //     usb_host_endpoint_clear(_host->deviceHandle(), ep->bEndpointAddress);
            // }
        }
    }
}

USBacmDevice::~USBacmDevice()
{
}

bool USBacmDevice::init()
{
    usb_device_info_t info = _host->getDeviceInfo();

    esp_err_t err = usb_host_transfer_alloc(64, 0, &xfer_write);
    if (err)
        log_w("-----usb_host_transfer_alloc xfer_write status: %02X", err);
    xfer_write->device_handle = _host->deviceHandle();
    xfer_write->context = this;
    xfer_write->callback = usb_write_cb;
    xfer_write->bEndpointAddress = ep_out->bEndpointAddress;

    err = usb_host_transfer_alloc(64, 0, &xfer_read);
    if (err)
        log_w("-----usb_host_transfer_alloc xfer_read status: %02X", err);
    xfer_read->device_handle = _host->deviceHandle();
    xfer_read->context = this;
    xfer_read->callback = usb_read_cb;
    xfer_read->bEndpointAddress = ep_in->bEndpointAddress;

    err = usb_host_transfer_alloc(info.bMaxPacketSize0, 0, &xfer_ctrl);
    if (err)
        log_w("-----usb_host_transfer_alloc xfer_ctrl status: %02X", err);
    xfer_ctrl->device_handle = _host->deviceHandle();
    xfer_ctrl->context = this;
    xfer_ctrl->callback = usb_ctrl_cb;
    xfer_ctrl->bEndpointAddress = 0;

    return true;
}

void USBacmDevice::release()
{
    int _offset = 0;
    esp_err_t err = ESP_OK;
    const usb_config_desc_t *config_desc = _host->getConfigurationDescriptor();


    for (size_t n = 0; n < config_desc->bNumInterfaces; n++) {
        err = usb_host_interface_release(_host->clientHandle(), _host->deviceHandle(), n);
        if (err)
            log_w("-----usb_host_interface_release status: %02X", err);
    }
}

void USBacmDevice::setControlLine(bool dtr, bool rts)
{
    USB_CTRL_REQ_CDC_SET_CONTROL_LINE_STATE((usb_setup_packet_t *)xfer_ctrl->data_buffer, 0, dtr, rts);
    xfer_ctrl->num_bytes = sizeof(usb_setup_packet_t) + ((usb_setup_packet_t *)xfer_ctrl->data_buffer)->wLength;
    esp_err_t err = usb_host_transfer_submit_control(_host->clientHandle(), xfer_ctrl);
    if (err)
        log_w("-----setControlLine:usb_host_transfer_submit_control: %02X", err);
}

void USBacmDevice::setLineCoding(uint32_t bitrate, uint8_t cf, uint8_t parity, uint8_t bits)
{
    line_coding_t data;
    data.dwDTERate = bitrate;
    data.bCharFormat = cf;
    data.bParityType = parity;
    data.bDataBits = bits;
    USB_CTRL_REQ_CDC_SET_LINE_CODING((usb_setup_packet_t *)xfer_ctrl->data_buffer, 0, bitrate, cf, parity, bits);
    memcpy(xfer_ctrl->data_buffer + sizeof(usb_setup_packet_t), &data, sizeof(line_coding_t));
    xfer_ctrl->num_bytes = sizeof(usb_setup_packet_t) + 7;
    esp_err_t err = usb_host_transfer_submit_control(_host->clientHandle(), xfer_ctrl);
    if (err)
        log_w("-----setLineCoding:usb_host_transfer_submit_control: %02X", err);
}

void USBacmDevice::getLineCoding()
{
    USB_CTRL_REQ_CDC_GET_LINE_CODING((usb_setup_packet_t *)xfer_ctrl->data_buffer, 0);
    xfer_ctrl->num_bytes = sizeof(usb_setup_packet_t) + ((usb_setup_packet_t *)xfer_ctrl->data_buffer)->wLength;
    esp_err_t err = usb_host_transfer_submit_control(_host->clientHandle(), xfer_ctrl);
    if (err)
        log_w("-----getLineCoding:usb_host_transfer_submit_control: %02X", err);
}

void USBacmDevice::clearCommFeature()
{
    USB_CTRL_REQ_CDC_CLEAR_COMM_FEATURE((usb_setup_packet_t *)xfer_ctrl->data_buffer, 0, 1, 1);
    xfer_ctrl->num_bytes = sizeof(usb_setup_packet_t) + ((usb_setup_packet_t *)xfer_ctrl->data_buffer)->wLength;
    esp_err_t err = usb_host_transfer_submit_control(_host->clientHandle(), xfer_ctrl);
    if (err)
        log_w("-----clearCommFeature:usb_host_transfer_submit_control: %02X", err);
}

void USBacmDevice::INDATA()
{
    if (!connected)
        return;
    xfer_read->num_bytes = 64;

    esp_err_t err = usb_host_transfer_submit(xfer_read);
    if (err)
    {
        log_w("-----usb_host_transfer_submit:INDATA: 0x%02x", err);
    }
}

void USBacmDevice::OUTDATA(uint8_t *data, size_t len)
{
    if (!connected)
        return;
    if (!len)
        return;
    xfer_write->num_bytes = len;
    memcpy(xfer_write->data_buffer, data, len);

    esp_err_t err = usb_host_transfer_submit(xfer_write);

    if (err)
    {
        log_w("-----usb_host_transfer_submit:OUTDATA: 0x%02x", err);
    }
}

bool USBacmDevice::isConnected()
{
    return connected;
}

void USBacmDevice::onEvent(cdc_event_cb_t _cb)
{
    if (nullptr == _cb) {
        connected = false;
    }
    event_cb = _cb;
}

void USBacmDevice::_callback(int event, usb_transfer_t *transfer)
{
    switch (event)
    {
    case CDC_DATA_IN:
        if (event_cb)
            event_cb(event, transfer->data_buffer, transfer->actual_num_bytes);
        break;
    case CDC_DATA_OUT:
        if (event_cb)
            event_cb(event, transfer->data_buffer, transfer->actual_num_bytes);
        break;
    case CDC_CTRL_SET_LINE_CODING:
        if (event_cb)
            event_cb(event, NULL, transfer->actual_num_bytes);
        connected = true;
        log_w("USBacmDevice connected.\n");
        break;
    case CDC_CTRL_SET_CONTROL_LINE_STATE:
        if (event_cb)
            event_cb(event, NULL, transfer->actual_num_bytes);
        // connected = true;
        // log_w("USBacmDevice connected.\n");
        break;
    default:
        if (event_cb)
            event_cb(event, NULL, transfer->actual_num_bytes);

        break;
    }
}
