ZIV 5CTM esphome component
===============================

I wanted to read the [ZIV 5CTM smart
meter](https://www.zivautomation.com/products/metering-ami/prime-plc-smart-meter/)
that my electricity company provided at home to have good measurement on import and export of electricity.

The manual ([ZIV - Monofásico
5CTM.pdf](https://foroelectricidad.com/download/file.php?id=4711)) says that
its Optical port (Infrared) has these parameters:

- Optical port: as norm UNE EN 62056-21
- Signals: RX/TX
- Speed: 9600 baud
- ... trough the communication ports ... with the protocol DLMS/COSEM

From [Gurux](https://gurux.fi/) forums
([post1](https://www.gurux.fi/node/4819),
[post2](https://www.gurux.fi/forum/13263)) I learnt these extra information:

- DLMS protocol over HDLC interface
- 9600 baud, 8N1
- Client: 2
- Server: 144 = 0x90
- Password for Low authentication (ASCII string): 00000001

I tested the [gurux python](https://github.com/Gurux/Gurux.DLMS.Python) client example with a Raspberry Pi with a
[TTL 3.3V serial IR port](https://es.aliexpress.com/item/1005004914485309.html) and it worked.

Then I decided to learn how to bring that into esphome.

Example use
----------------

```yaml
external_components:
  - source: github://viric/esphome-ziv@master
    components: [ ziv ]

uart:
  id: mbus
  tx_pin: GPIO18
  rx_pin: GPIO19
  baud_rate: 9600
  data_bits: 8
  parity: NONE
  stop_bits: 1
  #debug:
  #  direction: BOTH
  #  after:
  #    delimiter: [ 0x7e ]

sensor:
  - platform: ziv
    update_interval: 60s
    import_active_energy:
      name: "Import active energy"
    export_active_energy:
      name: "Export active energy"
    import_active_power:
      name: "Import active power"
    export_active_power:
      name: "Export active power"
```

Any other DLMS/HDLC/COSEM
--------------

The code can be probably made to accomodate any other DLMS/HDLC/COSEM
electricity meter, by changing the connection parameters currently hardcoded.
But I don't have evidence of any other meter working like this, so I can't be
sure.

For example, maybe it can be made to work with meters that use _IEC 62056-21 mode E_,
currently not supported by [this Aquaticus esphome
module](https://aquaticus.info/iec62056.html).
