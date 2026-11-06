import android.hardware.HardwareBuffer;

public final class HardwareBufferJavaTest {
    public static void main(String[] arguments) {
        long usage = HardwareBuffer.USAGE_CPU_READ_OFTEN | HardwareBuffer.USAGE_CPU_WRITE_OFTEN;
        int failures = 0;
        int[] widths = {1, 3, 4095, 4096, 4097, 12345};
        for (int width : widths) {
            try (HardwareBuffer buffer = HardwareBuffer.create(width, 1, HardwareBuffer.BLOB, 1, usage)) {
                int actualWidth = buffer.getWidth();
                int actualHeight = buffer.getHeight();
                int actualLayers = buffer.getLayers();
                int actualFormat = buffer.getFormat();
                if (actualWidth != width || actualHeight != 1 || actualLayers != 1 ||
                        actualFormat != HardwareBuffer.BLOB) {
                    throw new IllegalStateException("Buffer description differs from request");
                }
                System.out.println("PASS Java BLOB width=" + width);
            } catch (Exception error) {
                String message = error.toString();
                System.out.println("FAIL Java BLOB width=" + width + ": " + message);
                ++failures;
            }
        }
        System.out.println("failures=" + failures);
        int status = failures == 0 ? 0 : 1;
        System.exit(status);
    }
}
