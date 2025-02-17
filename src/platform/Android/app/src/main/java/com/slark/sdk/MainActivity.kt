package com.slark.sdk

import androidx.appcompat.app.AppCompatActivity
import android.os.Bundle
import android.widget.TextView
import com.slark.sdk.databinding.ActivityMainBinding

class MainActivity : AppCompatActivity() {

private lateinit var binding: ActivityMainBinding

    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)

     binding = ActivityMainBinding.inflate(layoutInflater)
     setContentView(binding.root)

        // Example of a call to a native method
        binding.sampleText.text = stringFromJNI()
    }
    /**
      * A native method that is implemented by the 'sdk' native library,
      * which is packaged with this application.
      */
     external fun stringFromJNI(): String

     companion object {
         // Used to load the 'sdk' library on application startup.
         init {
             System.loadLibrary("sdk")
         }
     }
}