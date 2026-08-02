package com.fuzz.colors;

import android.content.Context;
import android.graphics.Canvas;
import android.graphics.Color;
import android.graphics.Paint;
import android.graphics.Path;
import android.util.AttributeSet;
import android.view.View;

import androidx.annotation.Nullable;

/**
 * Custom view that renders a heartbeat LED pattern with 18 individually controllable segments.
 * 1:1 path mapping matching the original SVG curve coordinates and proportions.
 * Supports static color, dual color (9/9 split), and a select-stroke outline traced on the
 * pattern itself (not a button-style border), driven frame-by-frame by LEDManager's shared
 * selection pulse so it blinks in the exact same phase/color as every other LED.
 */
public class HeartbeatPatternView extends View {

    // Paint for drawing segments
    private Paint segmentPaint;

    // Paint for the select-stroke outline traced along the same segment paths
    private Paint selectStrokePaint;

    // Colors for each of the 18 segments (default: gray/off)
    private int[] segmentColors = new int[18];

    // Default colors
    private static final int COLOR_OFF = 0xFF444444;
    private static final int COLOR_ON = 0xFFFF2A55;

    // Segment paths (18 segments based on pixel indices)
    private Path[] segmentPaths;

    // Pixel indices for each segment
    public static final int[] PIXEL_INDICES = {5, 8, 12, 18, 29, 50, 61, 67, 80, 108, 122, 126, 136, 154, 162, 165, 169, 173};

    // Selection state - outline color/width are pushed in every frame by LEDManager's
    // shared pulse animator (setSelectStroke), so this view never runs its own animation.
    private boolean isSelected = false;
    private int selectStrokeColor = 0x00FFFFFF;
    private float selectStrokeExtraWidthPx = 0f;

    // Dual color mode
    private boolean isDualColorMode = false;
    private int dualColorLeft = COLOR_ON;
    private int dualColorRight = COLOR_ON;
    
    public HeartbeatPatternView(Context context) { // Constructor with Context
        super(context); // Call superclass constructor
        init(); // Initialize view state and paths
    } // End of single-parameter constructor
    
    public HeartbeatPatternView(Context context, @Nullable AttributeSet attrs) { // Constructor with Attributes
        super(context, attrs); // Call superclass constructor
        init(); // Initialize view state and paths
    } // End of two-parameter constructor
    
    public HeartbeatPatternView(Context context, @Nullable AttributeSet attrs, int defStyleAttr) { // Constructor with style attributes
        super(context, attrs, defStyleAttr); // Call superclass constructor
        init(); // Initialize view state and paths
    } // End of three-parameter constructor
    
    private void init() {
        segmentPaint = new Paint();
        segmentPaint.setStyle(Paint.Style.STROKE);
        segmentPaint.setStrokeCap(Paint.Cap.ROUND);
        segmentPaint.setStrokeJoin(Paint.Join.ROUND);
        segmentPaint.setStrokeWidth(7.5f);
        segmentPaint.setAntiAlias(true);

        selectStrokePaint = new Paint();
        selectStrokePaint.setStyle(Paint.Style.STROKE);
        selectStrokePaint.setStrokeCap(Paint.Cap.ROUND);
        selectStrokePaint.setStrokeJoin(Paint.Join.ROUND);
        selectStrokePaint.setAntiAlias(true);

        for (int i = 0; i < segmentColors.length; i++) {
            segmentColors[i] = COLOR_OFF;
        }
        
        createSegmentPaths();
    }
    
    private void createSegmentPaths() { // Construct paths with exact 1:1 SVG bezier control points
        segmentPaths = new Path[18]; // Instantiate array of 18 Path objects
        
        // Segment 1: Pixel 5 (Baseline start line: 0 -> 25)
        Path p1 = new Path(); // Create path 1
        p1.moveTo(0f, 200f); // Move to starting canvas coordinate
        p1.lineTo(25f, 200f); // Draw horizontal line to pixel 5
        segmentPaths[0] = p1; // Assign to segment index 0
        
        // Segment 2: Pixel 8 (Upward curve of 1st small bump: 25 -> 40)
        Path p2 = new Path(); // Create path 2
        p2.moveTo(25f, 200f); // Move to start of bump
        p2.quadTo(32.5f, 155f, 40f, 155f); // Quad curve up to small peak
        segmentPaths[1] = p2; // Assign to segment index 1
        
        // Segment 3: Pixel 12 (Downward curve of 1st small bump: 40 -> 55)
        Path p3 = new Path(); // Create path 3
        p3.moveTo(40f, 155f); // Move to peak of small bump
        p3.quadTo(47.5f, 155f, 55f, 200f); // Quad curve down to baseline
        segmentPaths[2] = p3; // Assign to segment index 2
        
        // Segment 4: Pixel 18 (Baseline line: 55 -> 90)
        Path p4 = new Path(); // Create path 4
        p4.moveTo(55f, 200f); // Move to baseline connection point
        p4.lineTo(90f, 200f); // Draw straight line along baseline
        segmentPaths[3] = p4; // Assign to segment index 3
        
        // Segment 5: Pixel 29 (Rising cubic curve to Peak 1: 90 -> 140)
        Path p5 = new Path(); // Create path 5
        p5.moveTo(90f, 200f); // Move to start of rise
        p5.cubicTo(115f, 200f, 115f, 65f, 140f, 65f); // Cubic curve to Peak 1
        segmentPaths[4] = p5; // Assign to segment index 4
        
        // Segment 6: Pixel 50 (Descending curve to Valley 1: 140 -> 175)
        Path p6 = new Path(); // Create path 6
        p6.moveTo(140f, 65f); // Move to Peak 1
        p6.cubicTo(165f, 65f, 150f, 330f, 175f, 330f); // Cubic curve down to Valley 1
        segmentPaths[5] = p6; // Assign to segment index 5
        
        // Segment 7: Pixel 61 (Rising curve back to baseline: 175 -> 210)
        Path p7 = new Path(); // Create path 7
        p7.moveTo(175f, 330f); // Move to Valley 1
        p7.cubicTo(200f, 330f, 190f, 200f, 210f, 200f); // Cubic curve up to baseline
        segmentPaths[6] = p7; // Assign to segment index 6
        
        // Segment 8: Pixel 67 (Baseline line: 210 -> 255)
        Path p8 = new Path(); // Create path 8
        p8.moveTo(210f, 200f); // Move to baseline point
        p8.lineTo(255f, 200f); // Draw straight line along baseline
        segmentPaths[7] = p8; // Assign to segment index 7
        
        // Segment 9: Pixel 80 (Rising curve to Main Peak 2: 255 -> 305)
        Path p9 = new Path(); // Create path 9
        p9.moveTo(255f, 200f); // Move to start of main pulse
        p9.cubicTo(280f, 200f, 280f, 35f, 305f, 35f); // Cubic curve up to main peak
        segmentPaths[8] = p9; // Assign to segment index 8
        
        // Segment 10: Pixel 108 (Descending curve to Main Valley 2: 305 -> 350)
        Path p10 = new Path(); // Create path 10
        p10.moveTo(305f, 35f); // Move to main peak
        p10.cubicTo(330f, 35f, 325f, 365f, 350f, 365f); // Cubic curve down to lowest valley
        segmentPaths[9] = p10; // Assign to segment index 9
        
        // Segment 11: Pixel 122 (Rising curve back to baseline: 350 -> 390)
        Path p11 = new Path(); // Create path 11
        p11.moveTo(350f, 365f); // Move to main valley
        p11.cubicTo(375f, 365f, 370f, 200f, 390f, 200f); // Cubic curve up to baseline
        segmentPaths[10] = p11; // Assign to segment index 10
        
        // Segment 12: Pixel 126 (Baseline line: 390 -> 425)
        Path p12 = new Path(); // Create path 12
        p12.moveTo(390f, 200f); // Move to baseline connection point
        p12.lineTo(425f, 200f); // Straight line along baseline
        segmentPaths[11] = p12; // Assign to segment index 11
        
        // Segment 13: Pixel 136 (Rising curve to Peak 3: 425 -> 470)
        Path p13 = new Path(); // Create path 13
        p13.moveTo(425f, 200f); // Move to start of 3rd wave
        p13.cubicTo(450f, 200f, 445f, 75f, 470f, 75f); // Cubic curve up to Peak 3
        segmentPaths[12] = p13; // Assign to segment index 12
        
        // Segment 14: Pixel 154 (Descending curve to Valley 3: 470 -> 510)
        Path p14 = new Path(); // Create path 14
        p14.moveTo(470f, 75f); // Move to Peak 3
        p14.cubicTo(495f, 75f, 485f, 280f, 510f, 280f); // Cubic curve down to Valley 3
        segmentPaths[13] = p14; // Assign to segment index 13
        
        // Segment 15: Pixel 162 (Rising curve back to baseline: 510 -> 545)
        Path p15 = new Path(); // Create path 15
        p15.moveTo(510f, 280f); // Move to Valley 3
        p15.cubicTo(535f, 280f, 525f, 200f, 545f, 200f); // Cubic curve up to baseline
        segmentPaths[14] = p15; // Assign to segment index 14
        
        // Segment 16: Pixel 165 (Baseline line: 545 -> 565)
        Path p16 = new Path(); // Create path 16
        p16.moveTo(545f, 200f); // Move to baseline point
        p16.lineTo(565f, 200f); // Line along baseline
        segmentPaths[15] = p16; // Assign to segment index 15
        
        // Segment 17: Pixel 169 (Upward curve for final bump: 565 -> 580)
        Path p17 = new Path(); // Create path 17
        p17.moveTo(565f, 200f); // Move to start of final bump
        p17.quadTo(572.5f, 155f, 580f, 155f); // Quad curve up to final peak
        segmentPaths[16] = p17; // Assign to segment index 16
        
        // Segment 18: Pixel 173 (Downward curve and end baseline: 580 -> 615)
        Path p18 = new Path(); // Create path 18
        p18.moveTo(580f, 155f); // Move to peak of final bump
        p18.quadTo(587.5f, 155f, 595f, 200f); // Quad curve down to baseline
        p18.lineTo(615f, 200f); // Final line to end of view
        segmentPaths[17] = p18; // Assign to segment index 17
    } // End of createSegmentPaths method
    
    @Override
    protected void onDraw(Canvas canvas) {
        super.onDraw(canvas);
        
        float scaleX = getWidth() / 615f;
        float scaleY = getHeight() / 400f;
        
        canvas.save();
        canvas.scale(scaleX, scaleY);

        // Select-stroke outline traced along the same paths, underneath the segment
        // colors, so selecting HB highlights the pattern itself instead of a button box.
        if (isSelected && Color.alpha(selectStrokeColor) > 0) {
            selectStrokePaint.setColor(selectStrokeColor);
            selectStrokePaint.setStrokeWidth(
                    (segmentPaint.getStrokeWidth() + selectStrokeExtraWidthPx * 2) / scaleX);
            for (Path p : segmentPaths) {
                canvas.drawPath(p, selectStrokePaint);
            }
        }

        for (int i = 0; i < segmentPaths.length; i++) {
            segmentPaint.setColor(segmentColors[i]);
            canvas.drawPath(segmentPaths[i], segmentPaint);
        }

        canvas.restore();
    }
    
    /**
     * Set the color of a specific segment (0-17)
     * @param segmentIndex Segment index (0-17)
     * @param color Color to set (ARGB format)
     */
    public void setSegmentColor(int segmentIndex, int color) { // Set color for single segment
        if (segmentIndex >= 0 && segmentIndex < segmentColors.length) { // Check bounds
            segmentColors[segmentIndex] = color; // Assign new color value
            invalidate(); // Request redraw of view
        } // End bounds check
    } // End setSegmentColor method
    
    /**
     * Turn on a specific segment (sets to red)
     * @param segmentIndex Segment index (0-17)
     */
    public void turnOnSegment(int segmentIndex) { // Activate single segment
        setSegmentColor(segmentIndex, COLOR_ON); // Set segment to ON color
    } // End turnOnSegment method
    
    /**
     * Turn off a specific segment (sets to gray)
     * @param segmentIndex Segment index (0-17)
     */
    public void turnOffSegment(int segmentIndex) { // Deactivate single segment
        setSegmentColor(segmentIndex, COLOR_OFF); // Set segment to OFF color
    } // End turnOffSegment method
    
    /**
     * Set color using RGB values
     * @param segmentIndex Segment index (0-17)
     * @param r Red (0-255)
     * @param g Green (0-255)
     * @param b Blue (0-255)
     */
    public void setSegmentColorRGB(int segmentIndex, int r, int g, int b) { // Set RGB color for segment
        int color = 0xFF000000 | (r << 16) | (g << 8) | b; // Calculate ARGB integer
        setSegmentColor(segmentIndex, color); // Apply color
    } // End setSegmentColorRGB method
    
    /**
     * Turn off all segments
     */
    public void turnOffAll() { // Turn off all 18 segments
        for (int i = 0; i < segmentColors.length; i++) { // Loop through segment array
            segmentColors[i] = COLOR_OFF; // Set each to OFF color
        } // End loop
        invalidate(); // Request redraw of view
    } // End turnOffAll method
    
    /**
     * Turn on all segments
     */
    public void turnOnAll() { // Turn on all 18 segments
        for (int i = 0; i < segmentColors.length; i++) { // Loop through segment array
            segmentColors[i] = COLOR_ON; // Set each to ON color
        } // End loop
        invalidate(); // Request redraw of view
    } // End turnOnAll method
    
    /**
     * Get the number of segments
     * @return Total segments (18)
     */
    public int getSegmentCount() { // Get total count of segments
        return segmentColors.length; // Return segment count
    } // End getSegmentCount method
    
    /**
     * Get the pixel index for a given segment
     * @param segmentIndex Segment index (0-17)
     * @return Pixel index from HB MAP
     */
    public int getPixelIndex(int segmentIndex) {
        if (segmentIndex >= 0 && segmentIndex < PIXEL_INDICES.length) {
            return PIXEL_INDICES[segmentIndex];
        }
        return -1;
    }
    
    public void setStaticColor(int color) {
        isDualColorMode = false;
        for (int i = 0; i < segmentColors.length; i++) {
            segmentColors[i] = color;
        }
        invalidate();
    }
    
    public void setDualColor(int leftColor, int rightColor) {
        isDualColorMode = true;
        dualColorLeft = leftColor;
        dualColorRight = rightColor;
        
        for (int i = 0; i < segmentColors.length; i++) {
            if (i < 9) {
                segmentColors[i] = leftColor;
            } else {
                segmentColors[i] = rightColor;
            }
        }
        invalidate();
    }
    
    /**
     * Marks HB as selected/deselected. The actual blink color+width is pushed
     * every frame via {@link #setSelectStroke(int, float)} by LEDManager's shared
     * pulse animator - this just toggles whether that outline is drawn at all,
     * so deselecting hides it immediately regardless of the last pushed alpha.
     */
    public void setSelected(boolean selected) {
        this.isSelected = selected;
        if (!selected) {
            selectStrokeColor = 0x00FFFFFF;
        }
        invalidate();
    }

    /**
     * Called every frame by LEDManager's shared selection-pulse animator (the
     * same one that drives every other LED's blinking border), so HB's outline
     * stays in identical phase and color instead of running its own animation.
     *
     * @param argbColor      Current pulse color, alpha included (e.g. accent color
     *                       breathing between the shared min/max alpha).
     * @param extraWidthPx   Extra outline width added on each side of the segment
     *                       line, in real pixels (same value used for other LEDs' stroke).
     */
    public void setSelectStroke(int argbColor, float extraWidthPx) {
        this.selectStrokeColor = argbColor;
        this.selectStrokeExtraWidthPx = extraWidthPx;
        invalidate();
    }
}