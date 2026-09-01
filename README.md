# Stat Display
This displays the current stats of your computer such as cpu usage, cpu temp, and gpu usage, I made it becuase I've always wanted a stat monitor, I used fusion for the cad design and I wrote the code in vs code utilizing lvgl libraries to make the ui look good. This is assembled by placing the screen in its place, placing the arduino in its place, plugging the display into the arduino, putting the back plate on, and them screwing it in, done! For firmware, just flash the arduino side to the qualia and run the computer-side code.
<img width="1161" height="955" alt="Screenshot 2026-04-30 172103" src="https://github.com/user-attachments/assets/936c8275-3fae-4bb3-b6c8-dfa7287d33ce" />
<img width="1010" height="800" alt="image" src="https://github.com/user-attachments/assets/aabeebc9-72f5-430c-a1d1-d2172d462fae" />
<img width="511" height="538" alt="Screenshot 2026-04-27 185149" src="https://github.com/user-attachments/assets/9f2073cd-51ac-4729-beb8-8f21c66e8ecc" />
<img width="523" height="652" alt="image" src="https://github.com/user-attachments/assets/09c14b52-2de4-4c0b-8105-48adffce0baf" />


## Bill of Materials (BOM)

| Name        | Purpose                              | Quantity | Total Cost (USD) | Link                                      | Distributor   |
|-------------|--------------------------------------|---------:|-----------------:|-------------------------------------------|--------------|
| 4.0 Inch TFT LCD Screen Module Square 480*480 3SPI RGB 40Pin 3.3V | Display the stats from the computer  | 1        | 8.43            | (https://www.aliexpress.com/item/3256805498947783.html?spm=a2g0o.cart.0.0.4c0838dazTspG4&mp=1&pdp_npi=6%40dis%21USD%21USD%208.43%21USD%208.43%21%21USD%208.43%21%21%21%402101e2b617818992473175370e368d%2112000034005951369%21ct%21US%217774796089%21%211%210%21&pdp_ext_f=%7B%22cart2PdpParams%22%3A%7B%22pdpBusinessMode%22%3A%22retail%22%7D%7D)     | Aliexpress     |
| Adafruit Qualia ESP32-S3     | The brains of the device             | 1        | 19.95            | https://www.adafruit.com/product/5800     | Adafruit     |
