from PIL import Image
base=r'C:\Users\James\AppData\Local\Temp\eswreview\disp_img'
im=Image.open(base+r'\refcirc_hi-22.png')
W,H=im.size
# FPC connector + driver supply caps region (left-center), zoom
crop=im.crop((int(W*0.05), int(H*0.33), int(W*0.42), int(H*0.60)))
crop=crop.resize((int(crop.size[0]*1.6),int(crop.size[1]*1.6)))
crop.save(base+r'\fpc_zoom.png')
print('fpc',crop.size)
