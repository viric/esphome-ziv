#include "ziv.h"
#include <esphome/core/log.h>

namespace esphome {
namespace ziv {

ZivComponent::ZivComponent()
    :buffers_rr_(&settings_),
    aarq_rr_(&settings_),
    cosem_rr_(&settings_, "1.0.1.8.0.255")
{
}

void ZivComponent::setup() {
  cl_init(&settings_, true /*logicalname*/, 2, 0x90, DLMS_AUTHENTICATION_LOW,
          "00000001", DLMS_INTERFACE_TYPE_HDLC);
}

void ZivComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Ziv:");
  LOG_SENSOR("  ", "ImportEnergy", this->import_sensor_);
  LOG_UPDATE_INTERVAL(this);
  this->check_uart_settings(9600);
}

void ZivComponent::update() {
  if(state_ == State::IDLE)
  {
      nextcomm(State::SEND_BUFFERS);
  }
}

void ZivComponent::nextcomm(State state)
{
  switch(state)
  {
      case State::SEND_BUFFERS:
          rr_ = &buffers_rr_;
          break;
      case State::SEND_AARQ:
          rr_ = &aarq_rr_;
          break;
      case State::READ_IMPORT:
          cosem_rr_.setLogicalName("1.0.1.8.0.255");
          rr_ = &cosem_rr_;
          break;
      case State::READ_EXPORT:
          cosem_rr_.setLogicalName("1.0.2.8.0.255");
          rr_ = &cosem_rr_;
          break;
      default:
          ESP_LOGV(TAG, "nextcomm(IDLE) unexpected");
          return;
  }
  resetcomm();
  rr_->start(&messages_);
  for(int i=0; i < messages_.size; ++i)
  {
      ESP_LOGV(TAG, "nextcomm: message %d size %d", i, messages_.data[i]->size);
  }
  state_ = state;
}

void ZivComponent::loop() {
  switch(state_)
  {
    case State::IDLE:
        break;
    case State::SEND_BUFFERS:
        communicate();
        if (rr_->result().has_value())
        {
            if (*rr_->result() == 0)
            {
                nextcomm(State::SEND_AARQ);
            }
            else
            {
                ESP_LOGE(TAG, "State::SEND_BUFFERS error");
                state_ = State::IDLE;
            }
        }
        break;
    case State::SEND_AARQ:
        communicate();
        if (rr_->result().has_value())
        {
            if (*rr_->result() == 0)
            {
                nextcomm(State::READ_IMPORT);
            }
            else
            {
                ESP_LOGE(TAG, "State::SEND_AARQ error");
                state_ = State::IDLE;
            }
        }
        break;
    case State::READ_IMPORT:
        communicate();
        if (rr_ == &cosem_rr_ && rr_->result().has_value())
        {
            if (*rr_->result() == 0)
            {
                if (import_sensor_)
                    import_sensor_->publish_state(cosem_rr_.value());

                nextcomm(State::READ_EXPORT);
            }
            else
            {
                ESP_LOGE(TAG, "State::SEND_AARQ error");
                state_ = State::IDLE;
            }
        }
        break;
    case State::READ_EXPORT:
        communicate();
        if (rr_ == &cosem_rr_ && rr_->result().has_value())
        {
            if (*rr_->result() == 0)
            {
                if (export_sensor_)
                    export_sensor_->publish_state(cosem_rr_.value());
            }
            else
            {
                ESP_LOGE(TAG, "State::SEND_AARQ error");
            }
            state_ = State::IDLE;
        }
        break;
  }
}

float ZivComponent::get_setup_priority() const { return setup_priority::AFTER_WIFI; }

void ZivComponent::communicate()
{
    ESP_LOGV(TAG, "communicate() state=%d rr=%p", static_cast<int>(state_), rr_);
    if (mindex_ < messages_.size)
    {
        gxByteBuffer *buffer = messages_.data[mindex_];
        int maxsend = buffer->size - mdatapos_;
        if (maxsend > 0)
        {
            if (maxsend > 10)
                maxsend = 10;
            ESP_LOGV(TAG, "communicate: write_array index %d pos %d size %d",
                    mindex_, mdatapos_, maxsend);
            write_array(buffer->data + mdatapos_, maxsend);
            mdatapos_ += maxsend;
        }
        if (mdatapos_ >= buffer->size)
            mindex_++;
        if (mindex_ >= messages_.size)
        {
            timeout_ = millis() + 2000;
            ESP_LOGV(TAG, "communicate: message sent");
        }
    }
    else
    {
        if (millis() > timeout_)
        {
            ESP_LOGE(TAG, "communicate: ERROR timeout");
            rr_->error();
            return;
        }

        if (reply_.complete == 0)
        {
            ESP_LOGV(TAG, "communicate: reply not complete: pos %d size %d",
                    rbuffer_.position, rbuffer_.size);
            if (!eop_found_)
            {
                /* read until eop=0x7E */
                int lastReadPos = rbuffer_.position;

                do {
                    int navailable = available();
                    ESP_LOGV(TAG, "communicate: recv %d bytes", navailable);
                    if (navailable == 0)
                        break;

                    increase_rbuffer(navailable);

                    read_array(&rbuffer_.data[rbuffer_.size], navailable);
                    rbuffer_.size += navailable;
                } while(available() > 0);

                if (rbuffer_.size > 5)
                {
                    for(int i = rbuffer_.size-1; i != lastReadPos; --i)
                    {
                        if (rbuffer_.data[i] == 0x7E)
                        {
                            eop_found_ = true;
                            break;
                        }
                    }
                }
            }

            if (eop_found_)
            {
                ESP_LOGV(TAG, "communicate: eop_found: pos %d size %d", rbuffer_.position,
                        rbuffer_.size);
                int ret = dlms_getData2(&settings_, &rbuffer_, &reply_, 0);
                ESP_LOGV(TAG, "communicate: dlms_getData2 ret %d", ret);
                if (ret != 0)
                {
                    ESP_LOGE(TAG, "communicate: dlms_getData2 ret %d", ret);
                    rr_->error();
                    return;
                }
                eop_found_ = false;
            }
        }

        if (reply_.complete)
            rr_->parse(&reply_);
    }
}

}  // namespace ziv
}  // namespace esphome
