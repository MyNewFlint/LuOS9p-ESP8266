local stf=dofile "gstuff/menu.lua"

local menu_main = {
	{name="run HTTPd", act={ok=function() menus_loop=true; gui.exit(); end, }, },
	{name="Sign led", menu="menu/sgn.lua", ind_t=stf.ind, }, 
	{name="Led pwr", menu="menu/led.lua", ind_t=stf.ind, }, 
	{name="DC out", menu="menu/dc.lua", ind_t=stf.ind, }, 
	{name="AC key", menu="menu/ac.lua", ind_t=stf.ind, }, 
	{name="ADC in", menu="menu/adc.lua", ind_t=stf.ind, }, 
	{name="DAC out", menu="menu/dac.lua", ind_t=stf.ind, }, 
	{name="PWM out", menu="menu/pwm.lua", ind_t=stf.ind, }, 
	{name="Gpio in/out", menu="menu/pio.lua", ind_t=stf.ind, },
	{name="Other Sta/Ap", act={ok=function() menus_loop=false; sta_loop=true; gui.exit(); end, }, },
	{name="Exit menu", act={ok=function() menus_loop=false; gui.exit(); end, }, },
	{name=" ..redraw"}
};
stf=nil

return menu_main
