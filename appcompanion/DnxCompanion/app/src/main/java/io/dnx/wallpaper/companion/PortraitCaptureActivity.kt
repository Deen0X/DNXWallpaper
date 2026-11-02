package io.dnx.wallpaper.companion

import com.journeyapps.barcodescanner.CaptureActivity

/**
 * Escáner en vertical (el QR es cuadrado; no hace falta girar el móvil).
 * La actividad de zxing por defecto hereda la orientación del manifiesto de
 * la librería, que fuerza landscape.
 */
class PortraitCaptureActivity : CaptureActivity()
