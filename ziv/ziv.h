#pragma once

#include <esphome/core/component.h>
#include <esphome/components/sensor/sensor.h>
#include <esphome/components/uart/uart.h>
#include <dlmssettings.h>
#include <client.h>
#include <cosem.h>

namespace esphome {
namespace ziv {

static const char *const TAG = "ziv";

class FrameReceiver: public uart::UARTDevice
{
public:
    FrameReceiver()
    {
        BYTE_BUFFER_INIT(&rbuffer_);
        bb_capacity(&rbuffer_, 128);
        mes_init(&messages_);
        reply_init(&reply_);
        resetcomm();
    }

    void increase_rbuffer(int more)
    {
        if (rbuffer_.size + more > rbuffer_.capacity)
        {
            bb_capacity(&rbuffer_, 20 + rbuffer_.size + more);
        }
    }

    void resetcomm()
    {
        mes_clear(&messages_);
        reply_clear(&reply_);

        mindex_ = 0;
        mdatapos_ = 0;
        eop_found_ = false;

        rbuffer_.size = 0;
        rbuffer_.position = 0;
    }

    ~FrameReceiver()
    {
        bb_clear(&rbuffer_);
        mes_clear(&messages_);
        reply_clear(&reply_);
    }

protected:
    gxByteBuffer rbuffer_;
    message messages_;
    gxReplyData reply_;
    int mindex_;
    int mdatapos_;
    bool eop_found_;
    int timeout_;
};

class RequestResponse
{
public:
    virtual void parse(gxReplyData *reply) = 0;

    void error()
    {
        result_ = -1;
    }

    const optional<int> & result()
    {
        return result_;
    }

    void start(message *messages)
    {
        result_.reset();
        prepare_messages(messages);
    }

protected:
    virtual void prepare_messages(message *messages) = 0;

    RequestResponse(dlmsSettings *settings)
        :settings_(settings)
    {
    }

    dlmsSettings *settings_;
    optional<int> result_;
};

class BuffersReqResp: public RequestResponse
{
public:
    BuffersReqResp(dlmsSettings *settings)
        :RequestResponse(settings)
    {
    }

    void prepare_messages(message *messages) override
    {
        int ret = cl_snrmRequest(settings_, messages);
        if (ret != 0)
            ESP_LOGE(TAG, "cl_snrmRequest ERROR %d", ret);
    }

    void parse(gxReplyData *reply) override
    {
        result_ = cl_parseUAResponse(settings_, &reply->data);
        ESP_LOGV(TAG, "cl_parseUAResponse ret %d", *result_);
    }
};

class AarqReqResp: public RequestResponse
{
public:
    AarqReqResp(dlmsSettings *settings)
        :RequestResponse(settings)
    {
    }

    void prepare_messages(message *messages) override
    {
        int ret = cl_aarqRequest(settings_, messages);
        if (ret != 0)
            ESP_LOGE(TAG, "cl_aarqRequest ERROR %d", ret);
    }

    void parse(gxReplyData *reply) override
    {
        result_ = cl_parseAAREResponse(settings_, &reply->data);
        ESP_LOGV(TAG, "cl_parseAAREResponse ret %d", *result_);
    }
};

class CosemReqResp: public RequestResponse
{
public:
    CosemReqResp(dlmsSettings *settings, const char *logicname, unsigned char attribute=2)
        :RequestResponse(settings), attribute_(attribute), value_(0)
    {
        setLogicalName(logicname);
        ESP_LOGV(TAG, "CosemReqResp attr %d", attribute_);
    }

    CosemReqResp(const  CosemReqResp &) = delete;

    void setLogicalName(const char *logicname)
    {
        int ret = cosem_init(BASE(data_), DLMS_OBJECT_TYPE_REGISTER, logicname);
        if (ret != 0)
            ESP_LOGE(TAG, "cosem_init ERROR %d", ret);

        /* cosem_init overwrites attribute_! */
        ESP_LOGV(TAG, "setLogicalName attr %d", attribute_);
    }

    void prepare_messages(message *messages) override
    {
        ESP_LOGV(TAG, "cl_read(%p, %p, %d, %p)", settings_, BASE(data_), attribute_, messages);
        int ret = cl_read(settings_, BASE(data_), attribute_, messages);
        if (ret != 0)
            ESP_LOGE(TAG, "cl_read ERROR %d", ret);
    }

    void parse(gxReplyData *reply) override
    {
        result_ = cl_updateValue(settings_, BASE(data_), attribute_, &reply->dataValue);
        ESP_LOGV(TAG, "cl_updateValue ret %d", *result_);
        if (result_ == 0)
        {
            value_ = var_toInteger(&data_.value);
        }
    }

    int value() const
    {
        return value_;
    }

    gxRegister data_;
    unsigned char attribute_;
    int value_;
};

/* Ziv 5CTM electricity meter IR communication */
class ZivComponent : public PollingComponent, public FrameReceiver {
 public:
  ZivComponent();

  void set_import_energy_sensor(sensor::Sensor *sensor)
    { this->import_energy_sensor_ = sensor; }
  void set_export_energy_sensor(sensor::Sensor *sensor)
    { this->export_energy_sensor_ = sensor; }
  void set_import_power_sensor(sensor::Sensor *sensor)
    { this->import_power_sensor_ = sensor; }
  void set_export_power_sensor(sensor::Sensor *sensor)
    { this->export_power_sensor_ = sensor; }
  void setup() override;
  void dump_config() override;
  void loop() override;
  void update() override;

  float get_setup_priority() const override;

 protected:
  enum class State {
      IDLE,
      SEND_BUFFERS,
      SEND_AARQ,
      READ_IMPORT_ENERGY,
      READ_EXPORT_ENERGY,
      READ_IMPORT_POWER,
      READ_EXPORT_POWER,
  };
  void nextcomm(State state);
  void communicate();

  sensor::Sensor *import_energy_sensor_{nullptr};
  sensor::Sensor *export_energy_sensor_{nullptr};
  sensor::Sensor *import_power_sensor_{nullptr};
  sensor::Sensor *export_power_sensor_{nullptr};
  State state_{State::IDLE};
  State next_state_{State::IDLE};
  dlmsSettings settings_;

  BuffersReqResp buffers_rr_;
  AarqReqResp aarq_rr_;
  CosemReqResp cosem_rr_;
  RequestResponse *rr_{nullptr};
};

}  // namespace ziv
}  // namespace esphome
