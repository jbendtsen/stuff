package com.example.textwindowview;

import android.content.Context;
import android.util.AttributeSet;
import android.util.Log;
import android.view.View;
import android.view.MotionEvent;
import android.graphics.Canvas;
import android.graphics.Paint;

public class TextWindowView extends View {
    Context ctx = null;
    int[] buffer = null;
    int[] scratch = null;
    boolean allowWrap = true;
    boolean allowAutoScroll = true;
    boolean tailWasVisible = false;
    boolean touchMoving = false;
    float scrollAlpha = 1.0f;
    int capacityBits = 0;
    int head = 0;
    int tail = 0;
    int headLine = 0;
    int viewPos = 0;
    int viewLine = 0;
    int oldViewLine;
    float posX, posY;
    float oldPosX, oldPosY;
    float flingX, flingY;
    float touchX, touchY;
    float lastMoveX, lastMoveY;
    int backColor = 0xfff0f0f0;
    int textColor = 0xff404040;
    int scrollColor = 0x40202020;
    float textSize = 48.0f;
    float lineSpacing = 56.0f;
    final Object lock = new Object();

    public TextWindowView(Context ctx) {
        super(ctx);
        init(ctx);
    }
    public TextWindowView(Context ctx, AttributeSet attrs) {
        super(ctx, attrs);
        init(ctx);
    }

    void init(Context ctx) {
        this.ctx = ctx;
        allocateBufferLog2(14);
    }

    public void allocateBufferLog2(int n) {
        if (n <= 1 || n >= 32)
            return;

        synchronized (lock) {
            capacityBits = n;
            scratch = new int[1 << capacityBits];

            int[] newBuffer = new int[1 << capacityBits];
            int newTail = 0;
            if (buffer != null) {
                final int oldMask = buffer.length - 1;
                for (int i = head; i < tail && newTail < newBuffer.length; i++) {
                    newBuffer[newTail++] = buffer[i & oldMask];
                }
            }

            buffer = newBuffer;
            head = 0;
            headLine = 0;
            viewPos = 0;
            viewLine = 0;
            oldViewLine = 0;
            scrollAlpha = 1.0f;
            posX = 0.0f;
            posY = 0.0f;
            tailWasVisible = false;
            tail = newTail;
        }
    }

    public void setWrapping(boolean wrappingEnabled) {
        this.allowWrap = wrappingEnabled;
    }
    public void setAutoScroll(boolean autoScrollEnabled) {
        this.allowAutoScroll = autoScrollEnabled;
    }
    public void setTextSize(float textSize) {
        this.textSize = textSize;
    }
    public void setLineSpacing(float spacing) {
        this.lineSpacing = spacing;
    }
    public void setBackColor(int argb) {
        this.backColor = argb;
    }
    public void setTextColor(int argb) {
        this.textColor = argb;
    }
    public void setScrollColor(int argb) {
        this.scrollColor = argb;
    }

    public void addText(CharSequence text) {
        int[] data = text.codePoints().toArray();
        int len = Math.min(data.length, buffer.length - 1);
        if (!allowWrap)
            len = Math.min(len, buffer.length - tail);
        if (len <= 0)
            return;

        final int mask = (1 << capacityBits) - 1;
        synchronized (lock) {
            int oldViewPos = viewPos;

            int newTail = tail + len;
            int distance = 0;
            while (newTail - buffer.length > head && distance < buffer.length) {
                distance = 0;
                while (buffer[head & mask] != '\n' && distance < buffer.length) {
                    head++;
                    distance++;
                }
                boolean isNewLine = buffer[head & mask] == '\n';
                head++;
                headLine += isNewLine ? 1 : 0;
                viewLine += isNewLine && head > viewPos ? 1 : 0;
            }

            for (int i = 0; i < len; i++) {
                buffer[(tail+i) & mask] = data[i];
                if (allowAutoScroll && tailWasVisible && !touchMoving) {
                    if (data[i] == '\n')
                        posY += lineSpacing;
                }
            }

            tail = newTail;
            if (head > viewPos)
                viewPos = head;
        }

        postInvalidate();
    }

    @Override
    public boolean onTouchEvent(MotionEvent event) {
        int action = event.getAction();
        if (action == MotionEvent.ACTION_DOWN) {
            flingX = 0.0f;
            flingY = 0.0f;
            touchX = event.getX();
            touchY = event.getY();
            touchMoving = true;
            invalidate();
            return true;
        }

        if (action == MotionEvent.ACTION_MOVE) {
            float x = event.getX();
            float y = event.getY();
            float dX = touchX - x;
            float dY = touchY - y;
            touchX = x;
            touchY = y;
            lastMoveX = lastMoveX * 0.6f + dX * 0.4f;
            lastMoveY = lastMoveY * 0.6f + dY * 0.4f;
            flingX = 0.0f;
            flingY = 0.0f;
            posX = Math.max(0.0f, posX + dX);
            posY = Math.max(0.0f, posY + dY);
            invalidate();
            return true;
        }

        if (action == MotionEvent.ACTION_UP) {
            flingX = lastMoveX * 1.5f;
            flingY = lastMoveY;
            touchMoving = false;
            invalidate();
            return true;
        }

        return super.onTouchEvent(event);
    }

	@Override
	protected void onDraw(Canvas canvas) {
        Paint paint = new Paint();
        paint.setTextSize(textSize);
        paint.setColor(textColor);

        canvas.drawColor(backColor);

        boolean significantFling = flingX < -0.1 || flingX > 0.1 || flingY < -0.1 || flingY > 0.1;
        if (significantFling) {
            posX += flingX;
            if (posX < 0.0f) {
                posX = 0.0f;
                flingX = 0.0f;
            }
            else {
                float accel = Math.min(2.0f, Math.abs(flingX) / 5.0f);
                flingX -= flingX < 0.0f ? -accel : accel;
            }

            posY += flingY;
            if (posY < 0.0f) {
                posY = 0.0f;
                flingY = 0.0f;
            }
            else {
                float accel = Math.min(1.5f, Math.abs(flingY) / 5.0f);
                flingY -= flingY < 0.0f ? -accel : accel;
            }

            postInvalidate();
        }
        else {
            flingX = 0.0f;
            flingY = 0.0f;
        }

        float windowHeight = (float)this.getHeight();
        float windowWidth = (float)this.getWidth();
        float scrollPos = 0.0f;

        synchronized (lock) {
            //float deltaX = posX - oldPosX;
            //float deltaY = posY - oldPosY;
            float headY = (float)headLine * lineSpacing;
            posY = Math.max(posY, (float)headLine * lineSpacing);

            int newViewLine = (int)(posY / lineSpacing);
            int deltaLines = newViewLine - viewLine;
            final int mask = (1 << capacityBits) - 1;

            if (deltaLines > 0) {
                int pos = viewPos;
                int distance = 0;
                int count = 0;
                for (count = 0; count < deltaLines; count++) {
                    while (pos < tail && buffer[pos & mask] != '\n' && distance < buffer.length) {
                        pos++;
                        distance++;
                    }
                    if (pos >= tail)
                        break;

                    pos++;
                    distance = allowWrap ? 0 : distance + 1;
                }

                newViewLine = viewLine + count;
                if (pos >= tail)
                    posY = (float)newViewLine * lineSpacing;

                viewPos = pos;
            }
            else if (deltaLines < 0) {
                int pos = viewPos;
                int distance = 0;
                for (int i = 0; i < -deltaLines && distance < buffer.length; i++) {
                    pos--;
                    distance = allowWrap ? 0 : distance + 1;
                    while (pos >= 0 && buffer[pos & mask] != '\n' && distance < buffer.length) {
                        pos--;
                        distance++;
                    }
                }
                pos--;
                distance = allowWrap ? 0 : distance + 1;
                while (pos >= 0 && buffer[pos & mask] != '\n' && distance < buffer.length) {
                    pos--;
                    distance++;
                }
                pos++;
                viewPos = pos;
            }

            viewLine = newViewLine;

            int nLines = (int)Math.ceil(windowHeight / lineSpacing) + 1;
            int start = viewPos;
            int end = !allowWrap && tail > buffer.length ? buffer.length : tail;
            int pos = start;
            int distance = 0;
            float y = lineSpacing * (float)((int)(posY / lineSpacing)) - posY;

            tailWasVisible = false;
            for (int i = 0; i < nLines && start < end; i++) {
                distance = 0;
                while (buffer[pos & mask] != '\n' && pos < end && distance < buffer.length) {
                    scratch[distance++] = buffer[pos & mask];
                    pos++;
                }

                if (pos > start) {
                    String s = new String(scratch, 0, distance);
                    canvas.drawText(s, -posX + 8.0f, y + lineSpacing, paint);
                }
                if (pos >= end) {
                    tailWasVisible = true;
                    break;
                }

                y += lineSpacing;
                start = pos + 1;
                pos = start;
            }

            scrollPos = (float)(viewPos - head) / (float)(tail - head);
        }

        if (touchMoving || significantFling) {
            scrollAlpha = 1.0f;
        }

        if (scrollAlpha > 0.05f) {
            int color = (scrollColor >> 24) & 0xff;
            color = (int)((float)color * scrollAlpha);
            color = (color << 24) | (scrollColor & 0xffffff);
            paint.setColor(color);
            
            final float scrollFrac = 0.1f;
            float scrollY = scrollPos * (windowHeight * (1.0f - scrollFrac));
            canvas.drawRect(
                windowWidth * 0.97f,
                scrollY,
                windowWidth * 0.99f,
                scrollY + windowHeight * scrollFrac,
                paint
            );

            scrollAlpha -= 0.01f;
            postInvalidate();
        }

        oldPosX = posX;
        oldPosY = posY;
	}
}
