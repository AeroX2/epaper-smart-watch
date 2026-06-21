from PIL import Image
base=r'C:\Users\James\AppData\Local\Temp\eswreview\disp_img'
im=Image.open(base+r'\refcirc_hi-22.png')
W,H=im.size
crop=im.crop((0, int(H*0.30), W, int(H*0.64)))
crop.save(base+r'\refcirc_main.png')
cw,ch=crop.size
left=crop.crop((0,0,int(cw*0.55),ch))
left.save(base+r'\refcirc_left.png')
right=crop.crop((int(cw*0.45),0,cw,ch))
right.save(base+r'\refcirc_right.png')
print('main',crop.size,'left',left.size,'right',right.size)
