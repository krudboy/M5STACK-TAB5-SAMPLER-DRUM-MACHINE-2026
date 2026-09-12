/**
 * @brief Envía un mensaje MIDI de 3 bytes al dispositivo conectado.
 * 
 * @param status_byte El byte de estado (ej. 0x90 para Note On, 0x80 para Note Off).
 * @param channel Canal MIDI (1-16).
 * @param data1 Primer byte de datos (ej. número de nota).
 * @param data2 Segundo byte de datos (ej. velocidad).
 */
void send_midi_message(uint8_t status_byte, uint8_t channel, uint8_t data1, uint8_t data2) {
  // Comprobar si el dispositivo MIDI está listo para enviar datos
  if (!isMIDIReady || MIDIOut == NULL) {
    return;
  }

  uint8_t midi_packet[4];
  
  uint8_t command = status_byte & 0xF0;
  midi_packet[0] = command >> 4;
  midi_packet[1] = status_byte | (channel - 1);
  midi_packet[2] = data1;
  midi_packet[3] = data2;

  // Copiar el paquete al buffer de transferencia de salida
  memcpy(MIDIOut->data_buffer, midi_packet, 4);
  MIDIOut->num_bytes = 4;

  // Enviar
  esp_err_t err = usb_host_transfer_submit(MIDIOut);
  if (err != ESP_OK) {
    ESP_LOGE("send_midi_message", "Fallo al enviar el paquete MIDI: %x", err);
  }
}

const uint8_t real_key[32]={
24,25,26,27,28,29,30,31,
16,17,18,19,20,21,22,23,  
8,9,10,11,12,13,14,15,  
0,1,2,3,4,5,6,7
};

static void midi_transfer_cb(usb_transfer_t *transfer) {
  if (Device_Handle == transfer->device_handle) {
    // Comprobar si es una transferencia de ENTRADA
    if (transfer->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK) {
      if (transfer->status == 0) {
        uint8_t *const p = transfer->data_buffer;
        for (int i = 0; i < transfer->actual_num_bytes; i += 4) {
          parse_midi_message(&p[i]);
        }
        // Volver a poner el buffer a escuchar
        esp_err_t err = usb_host_transfer_submit(transfer);
        if (err != ESP_OK) {
          ESP_LOGE("", "usb_host_transfer_submit In fail: %x", err);
        }
      } else if (transfer->status != USB_TRANSFER_STATUS_NO_DEVICE) {
          ESP_LOGW("", "Transferencia IN con error, status %d", transfer->status);
      }
    } else {
      if (transfer->status != 0) {
        ESP_LOGW("", "Transferencia OUT con error, status %d", transfer->status);
      }
    }
  }
}

void check_interface_desc_MIDI(const void *p) {
  const usb_intf_desc_t *intf = (const usb_intf_desc_t *)p;
  if ((intf->bInterfaceClass == USB_CLASS_AUDIO) &&
      (intf->bInterfaceSubClass == 3) &&
      (intf->bInterfaceProtocol == 0))
  {
    isMIDI = true;
    ESP_LOGI("", "Dispositivo MIDI encontrado. Reclamando interfaz...");
    esp_err_t err = usb_host_interface_claim(Client_Handle, Device_Handle,
        intf->bInterfaceNumber, intf->bAlternateSetting);
    if (err != ESP_OK) ESP_LOGE("", "usb_host_interface_claim failed: %x", err);
  }
}

void prepare_endpoints(const void *p) {
  const usb_ep_desc_t *endpoint = (const usb_ep_desc_t *)p;
  esp_err_t err;

  if ((endpoint->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK) != USB_BM_ATTRIBUTES_XFER_BULK) {
    ESP_LOGW("", "Endpoint no es de tipo BULK: 0x%02x", endpoint->bmAttributes);
    return;
  }
  
  if (endpoint->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK) { // Endpoint IN
    ESP_LOGI("", "Configurando %d buffers para Endpoint IN 0x%02x...", MIDI_IN_BUFFERS, endpoint->bEndpointAddress);
    for (int i = 0; i < MIDI_IN_BUFFERS; i++) {
      err = usb_host_transfer_alloc(endpoint->wMaxPacketSize, 0, &MIDIIn[i]);
      if (err != ESP_OK) {
        MIDIIn[i] = NULL;
        ESP_LOGE("", "usb_host_transfer_alloc In fail: %x", err);
      }
      else {
        MIDIIn[i]->device_handle = Device_Handle;
        MIDIIn[i]->bEndpointAddress = endpoint->bEndpointAddress;
        MIDIIn[i]->callback = midi_transfer_cb;
        MIDIIn[i]->context = (void *)i;
        MIDIIn[i]->num_bytes = endpoint->wMaxPacketSize;
        err = usb_host_transfer_submit(MIDIIn[i]);
        if (err != ESP_OK) {
          ESP_LOGE("", "usb_host_transfer_submit In fail: %x", err);
        }
      }
    }
  }
  else { // Endpoint OUT
    ESP_LOGI("", "Configurando Endpoint OUT 0x%02x...", endpoint->bEndpointAddress);
    err = usb_host_transfer_alloc(endpoint->wMaxPacketSize, 0, &MIDIOut);
    if (err != ESP_OK) {
      MIDIOut = NULL;
      ESP_LOGE("", "usb_host_transfer_alloc Out fail: %x", err);
      return;
    }
    MIDIOut->device_handle = Device_Handle;
    MIDIOut->bEndpointAddress = endpoint->bEndpointAddress;
    MIDIOut->callback = midi_transfer_cb;
    MIDIOut->context = NULL;
  }

  if ((MIDIOut != NULL) && (MIDIIn[0] != NULL)) {
    isMIDIReady = true;
    ESP_LOGI("", "Dispositivo MIDI listo para enviar y recibir.");
  }
}

////////////////////////////////////////////////////////////////////////////// USB HID (keyboard / macropad / joystick)

// The Tab5 has one USB host port, so a device is either a MIDI controller or
// an HID device. Unlike MIDI, HID devices often expose several interfaces at
// once — a macropad typically puts its keys on a boot keyboard interface and
// its knob on a Consumer Control one — so every HID interface is claimed and
// polled rather than just the first.

// Newest-first log of raw reports, for the USB KBD monitor panel.
void hid_log_report(uint8_t iface, const uint8_t *data, uint8_t len) {
  if (len > HID_IN_BUFFER_SIZE) len = HID_IN_BUFFER_SIZE;
  for (int8_t l = HID_LOG_LINES - 1; l > 0; l--) {
    memcpy(hidLogBytes[l], hidLogBytes[l - 1], HID_IN_BUFFER_SIZE);
    hidLogLen[l] = hidLogLen[l - 1];
    hidLogIface[l] = hidLogIface[l - 1];
  }
  memcpy(hidLogBytes[0], data, len);
  hidLogLen[0] = len;
  hidLogIface[0] = iface;
  hidReportCount++;
  refresh_hid_monitor = true;
}

void hid_transfer_cb(usb_transfer_t *transfer) {
  if (Device_Handle != transfer->device_handle) return;

  uint8_t idx = (uint8_t)(uintptr_t)transfer->context;
  if (idx >= MAX_HID_IFACES) return;

  if (transfer->status != 0) {
    hidPolling[idx] = false;  // let the poll loop retry
    if (transfer->status == USB_TRANSFER_STATUS_NO_DEVICE) {
      // Unplugged. The host stack owns the transfer once the device is gone,
      // so drop our pointer instead of resubmitting into freed memory — that
      // is what crashed when the macropad was pulled out.
      HidIn[idx] = NULL;
      hidDeviceGone = true;
    } else {
      ESP_LOGW("", "HID transfer iface %d status %d", idx, transfer->status);
    }
    return;
  }

  if (transfer->actual_num_bytes > 0) {
    hid_log_report(idx, transfer->data_buffer, transfer->actual_num_bytes);

    // Only boot keyboard reports have the fixed 8-byte modifier/keycode
    // layout. Anything else is treated as a Consumer Control interface (a
    // media knob), and still logged raw for the monitor either way.
    if (hidIsBootKeyboard[idx] && transfer->actual_num_bytes == 8) {
      usb_kbd_handle_report(idx, transfer->data_buffer);
    } else {
      usb_consumer_handle_report(idx, transfer->data_buffer,
                                 (uint8_t)transfer->actual_num_bytes);
    }
  }

  // Resubmit straight away so a transfer is always in flight. The host
  // controller already paces interrupt endpoints at bInterval; gating this
  // behind our own timer instead dropped reports between polls, and losing
  // the key-up between two presses of the same key made a rotary knob read
  // as one held key per two clicks.
  esp_err_t err = usb_host_transfer_submit(transfer);
  if (err != ESP_OK) {
    hidPolling[idx] = false;
    ESP_LOGW("", "HID resubmit iface %d fail: %x", idx, err);
  }
}

// Claims an HID interface. Returns the slot index, or -1 if not claimed.
int8_t check_interface_desc_hid(const void *p) {
  const usb_intf_desc_t *intf = (const usb_intf_desc_t *)p;
  if (intf->bInterfaceClass != USB_CLASS_HID) return -1;
  if (hidIfaceCount >= MAX_HID_IFACES) return -1;

  esp_err_t err = usb_host_interface_claim(Client_Handle, Device_Handle,
      intf->bInterfaceNumber, intf->bAlternateSetting);
  if (err != ESP_OK) {
    ESP_LOGE("", "usb_host_interface_claim (hid) failed: %x", err);
    return -1;
  }

  uint8_t idx = hidIfaceCount++;
  hidIfaceNumber[idx] = intf->bInterfaceNumber;
  hidIsBootKeyboard[idx] = (intf->bInterfaceSubClass == 1 && intf->bInterfaceProtocol == 1);
  isKeyboard = true;
  ESP_LOGI("", "HID iface %d claimed (num %d, sub %d, proto %d)", idx,
      intf->bInterfaceNumber, intf->bInterfaceSubClass, intf->bInterfaceProtocol);
  return (int8_t)idx;
}

void prepare_endpoint_hid(const void *p, uint8_t idx) {
  const usb_ep_desc_t *endpoint = (const usb_ep_desc_t *)p;
  if (idx >= MAX_HID_IFACES || HidIn[idx] != NULL) return;

  // HID devices report over an interrupt IN endpoint.
  if ((endpoint->bmAttributes & USB_BM_ATTRIBUTES_XFERTYPE_MASK) != USB_BM_ATTRIBUTES_XFER_INT) return;
  if (!(endpoint->bEndpointAddress & USB_B_ENDPOINT_ADDRESS_EP_DIR_MASK)) return;

  // An IN transfer's buffer must be at least the endpoint's max packet size,
  // and num_bytes must be a whole multiple of it, or the USB host layer
  // asserts and panics. So allocate exactly MPS and request exactly MPS —
  // never clamp below it. (Clamping to a fixed 16 was what crashed on any
  // keyboard whose endpoint reports 64.)
  uint16_t mps = endpoint->wMaxPacketSize;
  if (mps == 0 || mps > HID_MAX_PACKET) {
    ESP_LOGW("", "HID iface %d: unusable max packet size %d, skipping", idx, mps);
    return;
  }

  esp_err_t err = usb_host_transfer_alloc(mps, 0, &HidIn[idx]);
  if (err != ESP_OK) {
    HidIn[idx] = NULL;
    ESP_LOGE("", "usb_host_transfer_alloc (hid) fail: %x", err);
    return;
  }
  HidIn[idx]->device_handle = Device_Handle;
  HidIn[idx]->bEndpointAddress = endpoint->bEndpointAddress;
  HidIn[idx]->callback = hid_transfer_cb;
  HidIn[idx]->context = (void *)(uintptr_t)idx;
  hidPacketSize[idx] = mps;
  hidInterval[idx] = endpoint->bInterval ? endpoint->bInterval : 10;
  ESP_LOGI("", "HID iface %d ready (ep 0x%02x, mps %d, %d ms)", idx,
      endpoint->bEndpointAddress, mps, hidInterval[idx]);
}

// Transfers resubmit themselves from the completion callback, so this only
// gets one started per interface and recovers if one ever falls over — hence
// the retry backoff rather than a per-report poll interval.
// Clears enumeration state after a disconnect so replugging starts clean and
// re-claims interfaces, instead of leaving stale ones behind.
void usb_hid_reset() {
  hidDeviceGone = false;
  for (uint8_t i = 0; i < MAX_HID_IFACES; i++) {
    HidIn[i] = NULL;
    hidPolling[i] = false;
    hidIsBootKeyboard[i] = 0;
  }
  hidIfaceCount = 0;
  isKeyboard = false;
  isMIDI = false;
  isMIDIReady = false;
  ESP_LOGI("", "USB device removed, HID state reset");
}

void usb_keyboard_poll() {
  if (hidDeviceGone) usb_hid_reset();

  for (uint8_t idx = 0; idx < hidIfaceCount; idx++) {
    if (HidIn[idx] == NULL || hidPolling[idx]) continue;
    if ((millis() - hidLastPoll[idx]) < 50) continue;  // retry backoff

    hidLastPoll[idx] = millis();
    HidIn[idx]->num_bytes = hidPacketSize[idx];
    esp_err_t err = usb_host_transfer_submit(HidIn[idx]);
    if (err == ESP_OK) {
      hidPolling[idx] = true;
    } else {
      ESP_LOGW("", "usb_host_transfer_submit (hid %d) fail: %x", idx, err);
    }
  }
}

void show_config_desc_full(const usb_config_desc_t *config_desc)
{
  const uint8_t *p = &config_desc->val[0];
  uint8_t bLength;
  int8_t currentHid = -1;  // HID slot the endpoints that follow belong to
  for (int i = 0; i < config_desc->wTotalLength; i+=bLength, p+=bLength) {
    bLength = *p;
    if ((i + bLength) <= config_desc->wTotalLength) {
      const uint8_t bDescriptorType = *(p + 1);
      switch (bDescriptorType) {
        case USB_B_DESCRIPTOR_TYPE_INTERFACE: {
          const usb_intf_desc_t *intf = (const usb_intf_desc_t *)p;
          currentHid = -1;
          if (intf->bInterfaceClass == USB_CLASS_HID) {
            currentHid = check_interface_desc_hid(p);
          } else if (!isMIDI) {
            check_interface_desc_MIDI(p);
          }
          break;
        }
        case USB_B_DESCRIPTOR_TYPE_ENDPOINT:
          if (currentHid >= 0) {
            prepare_endpoint_hid(p, (uint8_t)currentHid);
          } else if (isMIDI && !isMIDIReady) {
            prepare_endpoints(p);
          }
          break;
        default:
          break;
      }
    }
    else {
      ESP_LOGE("", "Descriptor USB invalido!");
      return;
    }
  }
}

////////////////////////////////////////////////////////////////////////////////////////////////////////////////////// Procesar mensaje MIDI

void parse_midi_message(const uint8_t* p) {
  if ((p[0] + p[1] + p[2] + p[3]) == 0) return;
  
  uint8_t cin = p[0] & 0x0F;
  uint8_t status = p[1];
  uint8_t channel = (status & 0x0F) + 1;
  uint8_t data1 = p[2];
  uint8_t data2 = p[3];

  switch (cin) {

    case 0x09: { ////////////////////////////////////////////////////////// Note On
      uint8_t note = data1;
      uint8_t velocity = data2;
      if (velocity > 0) {
        // The key map takes MIDI notes too, so a pad controller or keyboard
        // is learned by playing it, exactly like a USB device. Consumes the
        // note when mapping is armed or the note is bound; otherwise falls
        // through to the behaviour below.
        if (midi_note_run_action(note)) break;
        //Serial.printf("%3d, %3d, %2d\n", note, velocity, channel);  

        if (channel==1) {
          //Serial.printf("%3d, %3d, %2d\n", note, velocity, channel);
          if (note<8) {
       
          }

          if (note==91) { // play ("PLAY/PAUSE")
              if (playing){
                seq.stop();
                sstep=firstStep;
                recording=false;
                clearPADSTEP=true;
                pattern_song_counter=0; 
              } else {
                if (sync_state==2){ // if this machine is slave dont start playing now
                  pre_playing=true;
                } else {
                if (songing) pattern_song_counter=selected_pattern;
                  tick = 0;
                  seq.start();
                  sstep=firstStep;
                  refreshPADSTEP=true; 
                  Serial.println("go"); 
                }
              }
              playing=!playing; 
          } 
                 
          if (note==81) { // panic ("STOP ALL CLIPS")
            //MIDI.sendControlChange(ALL_NOTES_OFF, 127, 1);
            delay(1);
            //MIDI.sendControlChange(ALL_SOUND_OFF, 127, 1);
            delay(1);
            //MIDI.sendControlChange(RESET_ALL_CTRLS, 127, 1);
          }

          for (uint8_t f = 0; f < 5; f++) {
            if (note==mNN_pageRot[f]){
              //send_midi_led(old_cc_page_note, 0);
              pageRot=f;
              //send_midi_led(note, 1);
              old_cc_page_note=note;
              //Serial.println(note);
              if (pageRot<2) rPage=0;
              if (pageRot==2) rPage=1;
              if (pageRot>2) rPage=2;
              refreshMODES=true;
              refresh_rPage=true;

            }
          }
          
          // Sequencer 8 x 4 grid
          if (note>7 && note<40) {
            // uint8_t rnote=real_key[note-8];
            // if (pattern[rnote]!=0) {
            //   pattern[rnote]=0;
            //   // Turn oFF note LED
            //   send_midi_message(0x90, channel, note, 0);              
            // } else {
            //   // Turn on GREEN LED
            //   send_midi_message(0x90, channel, note, 1);
            //   pattern[rnote]=lastNotePlayed;
            // }
          }

        } else if (channel == Rebirth338Engine::ACID_A_CHANNEL ||
                   channel == Rebirth338Engine::ACID_B_CHANNEL ||
                   channel == Rebirth338Engine::DRUM_CHANNEL) {
          // ReBirth338: dedicated MIDI channels for the acid-303s / 808 kit,
          // kept separate from the 16 sample/synth tracks below.
          rebirth338_noteOn(channel, note, velocity);
        } else {
          //MIDI.sendNoteOn(note, 127, 1);
          //lastNotePlayed=note;
          synthESP32_TRIGGER_P(selected_sound,note);
          if (recording){
            bitWrite(pattern[selected_sound],sstep,1);
            melodic[selected_sound][sstep]=note;
          }
          //refreshPATTERN=true;
        }
      }
      break;
    }
    
    case 0x08: { ////////////////////////////////////////////////////////// Note Off
      uint8_t note = data1;
      uint8_t velocity = data2;
      //Serial.printf("Note OFF: Nota: %3d, Velocidad: %3d, Canal: %2d\n", note, velocity, channel);

      if (channel == Rebirth338Engine::ACID_A_CHANNEL ||
          channel == Rebirth338Engine::ACID_B_CHANNEL ||
          channel == Rebirth338Engine::DRUM_CHANNEL) {
        rebirth338_noteOff(channel, note);
      } else if (channel==2) {
        //MIDI.sendNoteOff(note, 0, 1);
      } else {
        if (note<8) {
          //send_midi_message(0x90, channel, note, 3); // Turn oN YELLOW LED
        }
      }
      break;
    }

    case 0x0B: { ////////////////////////////////////////////////////////// Control Change (CC)
      uint8_t controller = data1;
      uint8_t value = data2;

      if (channel == Rebirth338Engine::ACID_A_CHANNEL ||
          channel == Rebirth338Engine::ACID_B_CHANNEL) {
        rebirth338_controlChange(channel, controller, value);
        break;
      }

      // LEARN armed: the next CC that moves gets bound to the selected parameter.
      if (learn_armed) {
        learn_armed = false;
        refreshMODES = true;
        midi_learn_bind_cc(selected_rot, channel, controller);
        break;
      }

      // A learned binding wins; anything unlearned falls through to the
      // original fixed APC KEY25 page/CC mapping below.
      int learned_rot = midi_learn_lookup_cc(channel, controller);
      if (learned_rot >= 0) {
        selected_rot = learned_rot;
        select_rot();
        old_counter1 = counter1;
        counter1 = mapRounded(value, 0, 127, min_values[selected_rot], max_values[selected_rot]);
        if (rPage != mRot[learned_rot]->rPage) {
          rPage = mRot[learned_rot]->rPage;
          refreshMODES = true;
          refresh_rPage = true;
        }
        do_rot();
        break;
      }

      // Buscar controlador en los rot que correspondan a la página actual.
      for (int f = 0; f < MAX_BARS; f++) {
        if (mRot[f]->pageRot==pageRot && mRot[f]->cc==controller){
          selected_rot=f;
          select_rot();
          old_counter1=counter1;
          counter1=mapRounded(value,0,127,min_values[selected_rot],max_values[selected_rot]);
          //Serial.printf("counter1: %3d",counter1);
          if (rPage!=mRot[f]->rPage){
              rPage=mRot[f]->rPage;
              refreshMODES=true;
              refresh_rPage=true;
          }
          do_rot();
          break;
        }
      }

    }
    default: {
      Serial.printf("OTRO:     Paquete: %02x %02x %02x %02x\n", p[0], p[1], p[2], p[3]);
      break;
    }
  }
}
