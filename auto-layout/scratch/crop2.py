from PIL import Image
base=r'C:\Users\James\AppData\Local\Temp\eswreview\disp_img'
im=Image.open(base+r'\refcirc_hi-22.png')
W,H=im.size
# booster center: zoom tighter on diode/cap labels
crop=im.crop((int(W*0.55), int(H*0.38), W, int(H*0.58)))
crop=crop.resize((int(crop.size[0]*1.3),int(crop.size[1]*1.3)))
crop.save(base+r'\booster_zoom.png')
print('z',crop.size)
# top right: front light + Q1 gate area
crop2=im.crop((int(W*0.45), int(H*0.30), W, int(H*0.45)))
crop2.save(base+r'\fl_zoom.png')
print('fl',crop2.size)
