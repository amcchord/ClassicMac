#!/usr/bin/env lua

local function linear2srgb(x)
   if x <= 0.0031308 then return x * 12.92
   else return 1.055 * math.pow(x, 1/2.4) - 0.055
   end
end

local function srgb2linear(x)
   if x <= 0.04045 then return x / 12.92
   else return math.pow((x + 0.055) / 1.055, 2.4) end
end

local function mac2linear(x)
   return math.pow(x, 1.8)
end

local function ntsc2linear(x)
   return math.pow(x, 2.2)
end

local function sgi2linear(x)
   return math.pow(x, 2.41)
end

local function pal2linear(x)
   return math.pow(x, 2.8)
end

local f = io.open("src/gamma_tables.s", "wb")

local function outtable(name, id, str, func)
   f:write(name.."_SBlock: .long "..name.."_End-"..name.."_SBlock\n")
   f:write("        .short "..id.."\n")
   f:write(name.."_Name:\n")
   f:write("        .asciz \""..str.."\"\n")
   f:write("        .align 2\n");
   f:write(name..":\n")
   f:write("        .short 0 /* version */\n")
   f:write("        .short 0 /* type */\n")
   f:write("        .short 0 /* formula size */\n")
   f:write("        .short 1 /* channel count */\n")
   f:write("        .short 256 /* datum count */\n")
   f:write("        .short 8 /* datum width */\n")
   for n=0,255 do
      if n % 8 == 0 then
         f:write("        .byte ")
      else
         f:write(",")
      end
      local basic_value = n / 255
      local corrected_value = math.floor(func(basic_value) * 255 + 0.5)
      assert(corrected_value >= 0 and corrected_value <= 255)
      f:write(("0x%02X"):format(corrected_value))
      if n % 8 == 7 then
         f:write("\n")
      end
   end
   f:write(name.."_End:\n")
end

local function outtable_rgb(name, id, str, func)
   f:write(name.."_SBlock: .long "..name.."_End-"..name.."_SBlock\n")
   f:write("        .short "..id.."\n")
   f:write(name.."_Name:\n")
   f:write("        .asciz \""..str.."\"\n")
   f:write("        .align 2\n");
   f:write(name..":\n")
   f:write("        .short 0 /* version */\n")
   f:write("        .short 0 /* type */\n")
   f:write("        .short 0 /* formula size */\n")
   f:write("        .short 3 /* channel count */\n")
   f:write("        .short 256 /* datum count */\n")
   f:write("        .short 8 /* datum width */\n")
   for n=0,255 do
      if n % 4 == 0 then
         f:write("        .byte ")
      else
         f:write(",")
      end
      local basic_value = n / 255
      local r,g,b = func(basic_value)
      r = math.floor(r * 255 + 0.5)
      assert(r >= 0 and r <= 255)
      g = math.floor(g * 255 + 0.5)
      assert(g >= 0 and g <= 255)
      b = math.floor(b * 255 + 0.5)
      assert(b >= 0 and b <= 255)
      f:write(("0x%02X, 0x%02X, 0x%02X"):format(r, g, b))
      if n % 4 == 3 then
         f:write("\n")
      end
   end
   f:write(name.."_End:\n")
end

outtable("_GammaTableMac", 128, "Mac Standard Gamma (1.8)", function(x) return linear2srgb(mac2linear(x)) end)
outtable("_GammaTableLinear", 129, "Linear Gamma (1.0)", function(x) return linear2srgb(x) end)
outtable("_GammaTableNTSC", 130, "NTSC/PC Gamma (2.2)", function(x) return linear2srgb(ntsc2linear(x)) end)
outtable("_GammaTableSGI", 131, "SGI Gamma (2.41)", function(x) return linear2srgb(sgi2linear(x)) end)
outtable("_GammaTablePAL", 132, "PAL/SECAM Gamma (2.8)", function(x) return linear2srgb(pal2linear(x)) end)

local LIT_R = srgb2linear(0xf1/255)
local LIT_G = srgb2linear(0x80/255)
local LIT_B = srgb2linear(0x50/255)
local DIM_R = srgb2linear(0x22/255)
local DIM_G = srgb2linear(0x0c/255)
local DIM_B = srgb2linear(0x05/255)
local DIF_R = LIT_R - DIM_R
local DIF_G = LIT_G - DIM_G
local DIF_B = LIT_B - DIM_B
outtable_rgb("_GammaTablePlato", 133, "Amber", function(x)
   local i = mac2linear(x)
   local r = DIM_R + DIF_R * i
   local g = DIM_G + DIF_G * i
   local b = DIM_B + DIF_B * i
   return linear2srgb(r),linear2srgb(g),linear2srgb(b)
end)
outtable_rgb("_GammaTableOtalp", 134, "Amber (inverse)", function(x)
   local i = mac2linear(1-x)
   local r = DIM_R + DIF_R * i
   local g = DIM_G + DIF_G * i
   local b = DIM_B + DIF_B * i
   return linear2srgb(r),linear2srgb(g),linear2srgb(b)
end)
