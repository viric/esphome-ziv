/*
    esphome-ziv: esphome module for Ziv 5CTM electricity meters
    Copyright (C) 2023  Lluís Batlle i Rossell

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
    */
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
  /* Config seen in Gurux forums:
   * https://www.gurux.fi/node/4819
   * https://www.gurux.fi/forum/13263
   */
  cl_init(&settings_, true /*logicalname*/, 2, 0x90, DLMS_AUTHENTICATION_LOW,
          "00000001", DLMS_INTERFACE_TYPE_HDLC);
}

void ZivComponent::dump_config() {
  ESP_LOGCONFIG(TAG, "Ziv:");
  LOG_SENSOR("  ", "ImportEnergy", this->import_energy_sensor_);
  LOG_SENSOR("  ", "ExportEnergy", this->import_energy_sensor_);
  LOG_SENSOR("  ", "ImportPower", this->import_power_sensor_);
  LOG_SENSOR("  ", "ExportPower", this->import_power_sensor_);
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
      case State::READ_IMPORT_ENERGY:
          cosem_rr_.setLogicalName("1.0.1.8.0.255");
          rr_ = &cosem_rr_;
          break;
      case State::READ_EXPORT_ENERGY:
          cosem_rr_.setLogicalName("1.0.2.8.0.255");
          rr_ = &cosem_rr_;
          break;
      case State::READ_IMPORT_POWER:
          cosem_rr_.setLogicalName("1.0.1.7.0.255");
          rr_ = &cosem_rr_;
          break;
      case State::READ_EXPORT_POWER:
          cosem_rr_.setLogicalName("1.0.2.7.0.255");
          rr_ = &cosem_rr_;
          break;
      case State::IDLE:
          rr_ = nullptr;
          break;
      default:
          ESP_LOGE(TAG, "nextcomm(IDLE) unexpected");
          return;
  }
  resetcomm();
  if (rr_ != nullptr)
  {
      rr_->start(&messages_);
      for(int i=0; i < messages_.size; ++i)
      {
          ESP_LOGV(TAG, "nextcomm: message %d size %d", i, messages_.data[i]->size);
      }
  }
  state_ = state;
}

const char* ZivComponent::state_name(State state)
{
  switch(state)
  {
    case State::IDLE:
      return "IDLDE";
    case State::SEND_BUFFERS:
      return "SEND_BUFFERS";
    case State::SEND_AARQ:
      return "SEND_AARQ";
    case State::READ_IMPORT_ENERGY:
      return "READ_IMPORT_ENERGY";
    case State::READ_EXPORT_ENERGY:
      return "READ_EXPORT_ENERGY";
    case State::READ_IMPORT_POWER:
      return "READ_IMPORT_POWER";
    case State::READ_EXPORT_POWER:
      return "READ_EXPORT_POWER";
  }
  return "UNKNOWN";
}

void ZivComponent::loop() {
  State next_state = State::IDLE;
  sensor::Sensor* sensor = nullptr;;
  float scale = 1.f;

  switch(state_)
  {
    case State::IDLE:
      break;
    case State::SEND_BUFFERS:
      next_state = State::SEND_AARQ;
      break;
    case State::SEND_AARQ:
      next_state = State::READ_IMPORT_ENERGY;
      break;
    case State::READ_IMPORT_ENERGY:
      sensor = import_energy_sensor_;
      scale = 1.f; // Wh
      next_state = State::READ_EXPORT_ENERGY;
      break;
    case State::READ_EXPORT_ENERGY:
      sensor = export_energy_sensor_;
      scale = 1.f; // Wh
      next_state = State::READ_IMPORT_POWER;
      break;
    case State::READ_IMPORT_POWER:
      sensor = import_power_sensor_;
      scale = 10.f; // decawatts
      next_state = State::READ_EXPORT_POWER;
      break;
    case State::READ_EXPORT_POWER:
      sensor = export_power_sensor_;
      scale = 10.f; // decawatts
      next_state = State::IDLE;
      break;
  }

  if (state_ == State::IDLE)
    return;

  communicate();
  if (rr_->result().has_value())
  {
    if (*rr_->result() == 0)
    {
      if (sensor != nullptr)
      {
        sensor->publish_state(cosem_rr_.value() * scale);
      }
      nextcomm(next_state);
    }
    else
    {
      ESP_LOGE(TAG, "Error in state %s", state_name(state_));
      state_ = State::IDLE;
    }
  }
}

float ZivComponent::get_setup_priority() const { return setup_priority::DATA; }

void ZivComponent::communicate()
{
    ESP_LOGV(TAG, "communicate() state=%d rr=%p", static_cast<int>(state_), rr_);
    if (mindex_ < messages_.size)
    {
        gxByteBuffer *buffer = messages_.data[mindex_];
        int maxsend = buffer->size - mdatapos_;
        if (maxsend > 0)
        {
            /* The loop() shouldn't last more than 20ms they say, and at 9600bauds and no
             * UART tx buffer this means we have to send small pieces */
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

                /* This >5 is copied from Gurux Arduino Client example code.
                 * No idea why. */
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
