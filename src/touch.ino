// Holding the top-left SOUND button switches to the portrait ReBirth panel.
// Tracked separately from the tap handling below so a short press keeps its
// normal page-select behaviour.
#define REBIRTH_HOLD_MS 800
#define REBIRTH_HOLD_BUTTON 28  // SOUND, top-left

static unsigned long rb_hold_start = 0;
static bool rb_hold_fired = false;

static int8_t rb_hold_target = -1;

// Holding the top-left SOUND button opens the portrait ReBirth panel.
//
// The pad page is deliberately NOT on a pad hold any more: the pads fill the
// whole bottom half of the screen, so holding one while playing — which is
// completely normal — kept opening the page unasked. It's on SHIFT + pad
// instead, which is instant and can't happen by accident.
static void check_rebirth_hold(int x, int y) {
  Boton *b = mBoton[REBIRTH_HOLD_BUTTON];
  bool inside = (x > b->x) && (x < b->x + b->w) && (y > b->y) && (y < b->y + b->h);

  if (!inside) {
    rb_hold_target = -1;
    rb_hold_start = 0;
    rb_hold_fired = false;
    return;
  }
  if (rb_hold_start == 0) {
    rb_hold_start = millis();
    return;
  }
  if (!rb_hold_fired && (millis() - rb_hold_start) >= REBIRTH_HOLD_MS) {
    rb_hold_fired = true;
    rebirth_ui_active = true;
    rebirth_ui_mode_changed = true;
  }
}

void read_touch(){

  // Lee 1 punto de toque “raw”
  int n = M5.Display.getTouchRaw(tp, 1);
  if (n > 0) {

    // Convierte a píxeles
    M5.Display.convertRawXY(tp, n);
      cox=tp[0].x;
      coy=tp[0].y;

    check_rebirth_hold(cox, coy);

      //  Serial.print(cox);
      //  Serial.print(" ");
      //  Serial.println(coy);

    if (pad_page_active && !touchActivo) {
      touchActivo = true;
      if (pad_page_touch(cox, coy)) return;
    }

    if (!touchActivo){
      touchActivo = true; 


      for (byte f=0;f<MAX_BUTTONS;f++){
        if (mBoton[f]->rPage==0 || mBoton[f]->rPage==rPage) {
          if ( (cox > mBoton[f]->x) && (cox < (mBoton[f]->x+mBoton[f]->w)) && (coy > mBoton[f]->y) && (coy < (mBoton[f]->y+mBoton[f]->h)) ) {
            if (f==last_touched ){
              if (start_debounce+debounce_time > millis() ){
                break;
              } 
            }
            mBoton[f]->trigger_on=1;
            last_touched=f;
            start_debounce=millis();
            touchActivo = true;
            //  Serial.print("b ");
            //  Serial.println(f);
            break;
          }
        }
      }


      for (byte f=0;f<MAX_BARS;f++){
        if (mRot[f]->rPage==rPage){ // si el controlador pertenece a la pagina seleccionada
          if ( (cox > mRot[f]->x) && (cox < (mRot[f]->x+mRot[f]->w)) && (coy > mRot[f]->y) && (coy < (mRot[f]->y+mRot[f]->h)) ) {
            if (f==last_touched ){
              if (start_debounce+debounce_time > millis() ){
                break;
              } 
            }
            mRot[f]->trigger_on=1;
            last_touched=f;
            start_debounce=millis();
            touchActivo = true;
            //Serial.print("r ");
            //Serial.println(f);
            break;
          }
        }
      }

      for (byte f=0;f<16;f++){
        if ( (cox > mBseq[f]->x) && (cox < (mBseq[f]->x+mBseq[f]->w)) && (coy > mBseq[f]->y) && (coy < (mBseq[f]->y+mBseq[f]->h)) ) {
          if (f==last_touched ){
            if (start_debounce+debounce_time > millis() ){
              break;
            } 
          }
          mBseq[f]->trigger_on=1;
          last_touched=f;
          start_debounce=millis();
          touchActivo = true;
          //Serial.print("bs ");
          //Serial.println(f);
          break;
        }
      }
    }

  } else {
    touchActivo = false;
    rb_hold_start = 0;
    rb_hold_fired = false;
    rb_hold_target = -1;
  }

}