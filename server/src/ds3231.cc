/*
 * Copyright (C) 2025 Kernkonzept GmbH.
 * Author(s): Martin Kuettler martin.kuettler@kernkonzept.com
 *
 * License: see LICENSE.spdx (in this directory or the directories above)
 */

/**
 * \file   The DS3231 rtc. Is expected to always live on a I2C-Bus.
 */

// Documentation:
// https://www.analog.com/media/en/technical-documentation/data-sheets/ds3231.pdf

#include "rtc.h"

#include <l4/re/env>
#include <l4/re/error_helper>
#include <l4/i2c-driver/i2c_device_if.h>

#include <cstdio>
#include <cstring>
#include <time.h>

class DS3231_rtc : public Rtc
{
private:
  using raw_t = l4_uint8_t[7];

  struct Reg_addr
  {
    enum
      {
        Seconds           = 0x00,
        Minutes           = 0x01,
        Hours             = 0x02,
        Wday              = 0x03,
        Mday              = 0x04,
        Month_and_century = 0x05,
        Year              = 0x06,
      };
  };

  // binary coded decimal to binary
  static int bcd2bin(l4_uint8_t value)
  {
    return (value & 0x0f) +  10 * ((value & 0xf0) >> 4);
  }

  // seconds in range 0..59
  static int seconds(raw_t data)
  {
    return bcd2bin(data[Reg_addr::Seconds]);
  }

  // minutes in range 0..59
  static int minutes(raw_t data)
  {
    return bcd2bin(data[Reg_addr::Minutes]);
  }

  // hours in range 0..23
  static int hours(raw_t data)
  {
    if (data[Reg_addr::Hours] & 0x40) // am/pm format
      {
        int const value = data[Reg_addr::Hours];
        return (value & 0x0f)
          + ((value & 0x10) ? 10 : 0)
          + ((value & 0x20) ? 12 : 0);
      }
    else // 24h format
      {
        int const value = data[Reg_addr::Hours];
        return (value & 0x0f)
          + ((value & 0x10) ? 10 : 0)
          + ((value & 0x20) ? 20 : 0);
      }
  }

  // month day in range 0..31
  static int mday(raw_t data)
  {
    int const value = data[Reg_addr::Mday] & 0x3f;
    return bcd2bin(value);
  }

  // month in range 0..11
  static int month(raw_t data)
  {
    return bcd2bin(data[Reg_addr::Month_and_century] & 0x1f) - 1;
  }

  // year in range 100..299, representing 2000..2199 (offset 1900)
  static int year(raw_t data)
  {
    return bcd2bin(data[Reg_addr::Year])
      + ((data[Reg_addr::Month_and_century] & 0x80) ? 100 : 0)
      + 100;
  }

public:
  bool probe()
  {
    _ds3231 = L4Re::Env::env()->get_cap<I2c_device_ops>("ds3231");
    if (_ds3231.is_valid())
      {
        l4_uint64_t time;
        L4Re::chksys(get_time(&time));
        time_t secs = time / 1'000'000'000;
        printf("Found DS3231 RTC. Current time: %s\n",
               ctime(&secs));
        return true;
      }

    // TODO: check for alternative vbus containing i2c

    return false;
  }

  int set_time([[maybe_unused]] l4_uint64_t offset_nsec)
  {
    return 0;
  }

  int get_time(l4_uint64_t *offset_nsec)
  {
    enum : unsigned int
      {
        Size = 7
      };
    l4_uint8_t data[Size];

    if (_ds3231.is_valid())
      {
        l4_uint8_t addr = 0;
        L4::Ipc::Array<l4_uint8_t const> send_buffer{sizeof(addr), &addr};
        _ds3231->write(send_buffer);
        L4::Ipc::Array<l4_uint8_t> buffer{Size, data};
        _ds3231->read(buffer);
      }
    else
      {
        printf("Direct device access is needed for now\n");
        return -L4_ENODEV;
      }

    struct tm time;
    time.tm_sec = seconds(data);
    time.tm_min = minutes(data);
    time.tm_hour = hours(data);
    time.tm_mday = mday(data);
    time.tm_mon = month(data);
    time.tm_year = year(data);
    time.tm_wday = 0; // this is ignored
    time.tm_yday = 0;
    time.tm_isdst = 0;
    time.tm_gmtoff = 0;
    time.tm_zone = nullptr;
    l4_uint64_t offset_sec = timegm(&time);
    *offset_nsec = offset_sec * 1'000'000'000ul;
    return 0;
  }

private:
  L4::Cap<I2c_device_ops> _ds3231;
};

static DS3231_rtc _ds3231;
