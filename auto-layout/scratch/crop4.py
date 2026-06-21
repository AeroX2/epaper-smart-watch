from PIL import Image
base=r'C:\Users\James\AppData\Local\Temp\eswreview\disp_img'
im=Image.open(base+r'\refcirc_hi-22.png')
W,H=im.size
crop=im.crop((int(W*0.18), int(H*0.62), int(W*0.75), int(H*0.80)))
crop=crop.resize((int(crop.size[0]*1.4),int(crop.size[1]*1.4)))
crop.save(base+r'\tsensor_zoom.png')
print('ts',crop.size)
