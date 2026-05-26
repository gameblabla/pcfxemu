    if(fx_vce.dot_clock) // No cellophane in 7.16MHz pixel mode
    {
     const unsigned int outw = GetHighDotClockOutputWidth();
     const unsigned int vdcw = GetHighDotClockVDCWidth();
     for(unsigned int x = 0; x < outw; x++)
     {
      unsigned int vdc_x = (x * vdcw) / outw;
      if(vdc_x > 511) vdc_x = 511;
      const uint32 king_bg = (outw == 256) ? (bg_linebuffer + 8)[x] : SampleKING256Sharp(bg_linebuffer + 8, x, outw);
      const uint32 king_rb = (outw == 256) ? rainbow_linebuffer[x] : SampleKING256Sharp(rainbow_linebuffer, x, outw);
      LAYER_MIX_BODY_PIX(king_bg, king_rb, vdc_x);
      LAYER_MIX_FINAL_NOCELLO;
     }
    }
    else if((vce_rendercache.BLE & 0xC000) == 0xC000) // Front cellophane
    {
     uint8 CCR_Y_front = vce_rendercache.coefficient_mul_table_y[(vce_rendercache.coefficients[0] >> 8) & 0xF][(vce_rendercache.CCR >> 8) & 0xFF];
     int8 CCR_U_front = vce_rendercache.coefficient_mul_table_uv[(vce_rendercache.coefficients[0] >> 4) & 0xF][(vce_rendercache.CCR & 0xF0)];
     int8 CCR_V_front = vce_rendercache.coefficient_mul_table_uv[(vce_rendercache.coefficients[0] >> 0) & 0xF][(vce_rendercache.CCR << 4) & 0xF0];

     BPC_Cache = 0x008080 | (LAYER_NONE << 28);

     for(unsigned int x = 0; x < 256; x++)
     {
      LAYER_MIX_BODY(x, x);
      LAYER_MIX_FINAL_FRONT_CELLO;
     }
    }
    else if((vce_rendercache.BLE & 0xC000) == 0x4000) // Back cellophane
    {
     BPC_Cache = ((vce_rendercache.CCR & 0xFF00) << 8) | ((vce_rendercache.CCR & 0xF0) << 8) | ((vce_rendercache.CCR & 0x0F) << 4) | (LAYER_NONE << 28);

     for(unsigned int x = 0; x < 256; x++)
     {
      LAYER_MIX_BODY(x, x);
      LAYER_MIX_FINAL_BACK_CELLO;
     }
    }
    else if(ble_cache_any)		     // No front/back cello, but cellophane on at least 1 layer
    {
     for(unsigned int x = 0; x < 256; x++)
     {
      LAYER_MIX_BODY(x, x);
      LAYER_MIX_FINAL_CELLO
     }
    }
    else				     // No cellophane at all
    {
     for(unsigned int x = 0; x < 256; x++)
     {
      LAYER_MIX_BODY(x, x);
      LAYER_MIX_FINAL_NOCELLO
     }
    }

