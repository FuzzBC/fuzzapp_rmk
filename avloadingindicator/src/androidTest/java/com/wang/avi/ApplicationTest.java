import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.core.app.ApplicationProvider;
import org.junit.Test;
import org.junit.runner.RunWith;
import static org.junit.Assert.*;

@RunWith(AndroidJUnit4.class)
public class ApplicationTest {

    @Test
    public void testPreconditions() {
        // Use ApplicationProvider to get the application context
        assertNotNull(ApplicationProvider.getApplicationContext());
    }
}