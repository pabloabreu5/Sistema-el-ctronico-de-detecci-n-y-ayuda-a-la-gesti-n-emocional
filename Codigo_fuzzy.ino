//Librerias
#include <WiFi.h>
#include <Firebase_ESP_Client.h>
#include "addons/TokenHelper.h"
#include "addons/RTDBHelper.h"
#include <vector>
#include <algorithm>
#include <Wire.h>
#include "MAX30105.h"
#include "heartRate.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "spo2_algorithm.h"

//Definimos el Wifi
#define WIFI_SSID "MOVISTAR_4A80"
#define WIFI_PASSWORD "Xa3HTXYKETUYdH9Y3fVf"
//Definimos la base de datos
#define API_KEY "AIzaSyAhi45WAMxec0g5yRrf1HuTU7TLLiiA-bY"
#define DATABASE_URL "https://prueba2-503e6-default-rtdb.firebaseio.com/"  
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET    -1

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);
FirebaseData fbdo; // Objeto que gestiona las operaciones con la base de datos en tiempo real de Firebase (RTDB)
FirebaseAuth auth; // Objeto que contiene la información de autenticación (token, UID) del usuario de Firebase
FirebaseConfig config; // Objeto que contiene la configuración general del cliente Firebase (API key, URL, callbacks, etc.)



//DEFINIMOS VARIABLES

bool signupOK=false;
bool yaMedido = false;
std::vector<float> v_alegria, v_calma, v_cansancio, v_estres, v_ira, v_tristeza; // Vectores globales para almacenar cada emoción
// Strings para guardar la retroalimentacion
String retroalimentacion_mensual = ""; 
String retroalimentacion_semanal = "";
String retroalimentacion_diaria = "";
String retroalimentacion_semanal_ansiedad = "";
String retroalimentacion_mensual_ansiedad = "";
String retroalimentacion_semanal_depresion = "";
String retroalimentacion_mensual_depresion = "";
String mensajeFisiologico = "";

MAX30105 particleSensor;
const byte RATE_SIZE = 4;
byte rates[RATE_SIZE];
byte rateSpot = 0;
long lastBeat = 0;
int promedioFinal = 0;  
float beatsPerMinute;
float hrv;
int32_t beatAvg;
std::vector<float> datosBPM;
std::vector<long> intervalosRR; // Almacena los intervalos entre latidos (RR intervals)

float calcularHRV(const std::vector<long>& rr); //prototipos de funciones
std::vector<long> filtrarRR(const std::vector<long>& rr);

uint32_t irBuffer[100];    // Buffer para datos infrarrojos
uint32_t redBuffer[100];   // Buffer para datos rojo
int32_t spo2;              // Resultado de SpO2
int8_t validSPO2;          // Validez del SpO2
int32_t beatAvgSpO2;       // Resultado de BPM para SpO2
int8_t validHR;            // Validez del HR

struct Grados {
  float bajo;
  float medio;
  float alto;
};

void setup() {
  
  Serial.begin(115200);
  configTime(0, 0, "pool.ntp.org");
  Wire.begin(8, 9); // Pines I2C para ESP32-C3
  delay(1000);

  // Inicializa el sensor
  if (!particleSensor.begin(Wire, I2C_SPEED_FAST)) {
    Serial.println(" Sensor MAX30102 no encontrado. Revisa conexión.");
    while (1);
  }
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println(F(" No se detecta la pantalla OLED"));
    while (true);
  }

  particleSensor.setup(); // Configuración por defecto
  particleSensor.setPulseAmplitudeRed(0x0A); // Encender LED rojo
  particleSensor.setPulseAmplitudeGreen(0); // Apagar LED verde
  
  

  Serial.println(" Coloca el dedo sobre el sensor...");
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 10);
  display.println("Coloca el dedo sobre el sensor...");
  display.display();
  delay(5000);


  // Esperar hasta detectar dedo
  long irValue = particleSensor.getIR();
  display.clearDisplay();
  display.setCursor(0, 10);
  display.println("Esperando dedo...");
  display.display();
  
  do{
    Serial.println(" Esperando dedo...");
    delay(1000);
    irValue = particleSensor.getIR();
  }while (irValue < 10000); 

  Serial.println(" Dedo detectado. Iniciando medición. Mantenga 30 segundos...");
  display.clearDisplay();
  display.setCursor(5, 10);
  display.println("Dedo detectado");
  display.setCursor(5, 40);
  display.println("Iniciando medicion");
  display.setCursor(5, 50);
  display.println("Manten durante 30s");
  display.display();
  unsigned long startTime = millis();
  int validBeats = 0;
  int totalBPM = 0;

  while (millis() - startTime < 30000) {
    irValue = particleSensor.getIR();

    if (checkForBeat(irValue)) {
      long delta = millis() - lastBeat;
      lastBeat = millis();
      intervalosRR.push_back(delta);
      beatsPerMinute = 60 / (delta / 1000.0);

      if (beatsPerMinute > 30 && beatsPerMinute < 180) {
        rates[rateSpot++] = (byte)beatsPerMinute;
        rateSpot %= RATE_SIZE;

        // Promediar
        beatAvg = 0;
        for (byte i = 0 ; i < RATE_SIZE ; i++)
          beatAvg += rates[i];
        beatAvg /= RATE_SIZE;

        validBeats++;
        totalBPM += beatsPerMinute;
        datosBPM.push_back(beatsPerMinute);

        Serial.print(" BPM detectado: ");
        Serial.print(beatsPerMinute);
        Serial.print(" | Promedio: ");
        Serial.println(beatAvg);
      }
    }

    delay(20);
  }

  if (validBeats > 0) {
    promedioFinal = totalBPM / validBeats;
    Serial.print(" Medición terminada. BPM promedio: ");
    Serial.println(promedioFinal);
  } else {
    Serial.println(" No se detectaron pulsos válidos.");
  }

  

  hrv = calcularHRV(filtrarRR(intervalosRR));
  hrv = (int)hrv;
  Serial.print("HRV estimada: ");
  Serial.print(hrv);
  Serial.println(" ms");

  Serial.println("Iniciando análisis de SpO2...");

  for (int i = 0; i < 100; i++) {
    while (!particleSensor.available()) particleSensor.check();
    redBuffer[i] = particleSensor.getRed();
    irBuffer[i] = particleSensor.getIR();
    particleSensor.nextSample();
  }

  maxim_heart_rate_and_oxygen_saturation(irBuffer, 100, redBuffer, &spo2, &validSPO2, &beatAvgSpO2, &validHR);


  Serial.println("Proceso completo. No se realizarán más mediciones.");
  delay(3000);
  display.clearDisplay();
  display.setCursor(5, 10);
  display.println("Proceso completo.");
  display.display();  
  delay(3000);
  display.clearDisplay();
  display.setCursor(5, 0);
  display.println("Tus niveles son:");
  display.setCursor(5, 20);
  display.setTextSize(2);
  display.println(String(promedioFinal) + " PPM");
  display.display();  

  if (validSPO2) {
  Serial.print("SpO2 estimado: ");
  Serial.print(spo2);
  Serial.println(" %");

 
  display.setTextSize(2);
  display.setCursor(5,40);
  display.println("SpO2: " + String(spo2) + " %");
  display.display();
  } else {
  Serial.println("SpO2 no valido.");
  }

  

  
  // Conexión al WiFi
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.print("Conectando al WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
    delay(250);
    
  }
  
  Serial.println("\nWiFi conectado");

  // Configurar solo API Key y URL (sin auth)
  config.api_key = API_KEY;
  config.database_url = DATABASE_URL;

  Serial.print("Nuevo usuario...");
  if(Firebase.signUp(&config, &auth,"","")){   // Si el registro es exitoso, imprime confirmación y marca el flag de éxito
    Serial.println("ok");
    signupOK=true;
  }else{   // Si ocurre un error, imprime el mensaje detallado del fallo
    Serial.printf("%s\n",config.signer.signupError.message.c_str());
  }
  Serial.println("------------");

  config.token_status_callback= tokenStatusCallback; // Asigna la función de callback para mostrar el estado del token (útil para depuración)

  Firebase.begin(&config, &auth); // Inicializa la conexión del cliente Firebase con la configuración y la autenticación
  Firebase.reconnectWiFi(true);// Activa la reconexión automática al WiFi si se pierde la conexión

    

  // Subir todos los valores individuales de BPM desde el vector datosBPM
  for (size_t i = 0; i < datosBPM.size(); i++) {
    String pathDato = "/Usuarios/Pablo/DatosFisiologicos/RitmoCardiaco/Dato" + String(i);
    datosBPM[i] = (int)datosBPM[i];

    if (Firebase.RTDB.setFloat(&fbdo, pathDato, datosBPM[i])) {
      Serial.println("BPM " + String(i) + " subido: " + String(datosBPM[i], 0));
    } else {
      Serial.println("Error al subir Dato" + String(i) + ": " + fbdo.errorReason());
    }
  }


  // Subir la media
  String pathMedia = "/Usuarios/Pablo/DatosFisiologicos/PromedioPPM";
  if (Firebase.RTDB.setInt(&fbdo, pathMedia, promedioFinal)) {
    Serial.println("Promedio BPM subido: " + String(promedioFinal));
  } else {
    Serial.println("Error al subir promedio: " + fbdo.errorReason());
  }

  // Subir HRV a Firebase
  String pathHRV = "/Usuarios/Pablo/DatosFisiologicos/HRV";
  if (Firebase.RTDB.setFloat(&fbdo, pathHRV, hrv)) {
    Serial.println("HRV subida a Firebase");
  } else {
    Serial.println("Error al subir HRV: " + fbdo.errorReason());
  }

  // Subir SpO2 a Firebase
  String pathSpO2 = "/Usuarios/Pablo/DatosFisiologicos/SpO2";
  if (Firebase.RTDB.setInt(&fbdo, pathSpO2, spo2)) {
    Serial.println("SpO2 subido a Firebase");
  } else {
    Serial.println("Error al subir SpO2: " + fbdo.errorReason());
  }
  

}


void loop() {
  if (!yaMedido && Firebase.RTDB.getString(&fbdo, "/Usuarios/Pablo/Estado")) {
    if (fbdo.to<String>() == "true") {
      Serial.println("Comenzando la medición...");
      yaMedido = true;  // Para evitar repetir

      iniciarMedicion();
      yaMedido = false; 

      // Reseteas el valor de nuevo a "false" (como string también)
      Firebase.RTDB.setString(&fbdo, "/Usuarios/Pablo/Estado", "false");
    }
  } else {
    Serial.println("No se realizó la medición...");
  }

  delay(1000);
}


void iniciarMedicion() {
  obtenerTodasLasFechas();
  analizarEstadoMensual();
  analizarUltimaSemana();
  analizarUltimoDia();
  subirRetroalimentacion("Mensual", obtenerFechaYYYYMMDD(), retroalimentacion_mensual);
  subirRetroalimentacion("Semanal", obtenerFechaYYYYMMDD(), retroalimentacion_semanal);
  subirRetroalimentacion("Diario", obtenerFechaYYYYMMDD(), retroalimentacion_diaria);
  subirRetroalimentacion("RiesgoDepresionMensual", obtenerFechaYYYYMMDD(), retroalimentacion_mensual_depresion);
  subirRetroalimentacion("RiesgoDepresionSemanal", obtenerFechaYYYYMMDD(), retroalimentacion_semanal_depresion);
  subirRetroalimentacion("RiesgoAnsiedadMensual", obtenerFechaYYYYMMDD(), retroalimentacion_mensual_ansiedad);
  subirRetroalimentacion("RiesgoAnsiedadSemanal", obtenerFechaYYYYMMDD(), retroalimentacion_semanal_ansiedad);
  subirRetroalimentacion("EstadoFisiologico", obtenerFechaYYYYMMDD(), mensajeFisiologico);

}

// Función que obtiene todas las fechas registradas en Firebase bajo /Usuarios/Pablo/Resultados
// y extrae los valores de emociones correspondientes, almacenándolos en vectores globales
void obtenerTodasLasFechas() {
  String ruta = "/Usuarios/Pablo/Resultados";
  std::vector<String> fechas;  // Vector para guardar las claves (fechas) encontradas

  // Solicita las claves (fechas) sin descargar todos los datos (más rápido y ligero)
  if (Firebase.RTDB.getShallowData(&fbdo, ruta)) {
    FirebaseJson& json = fbdo.jsonObject(); // Objeto JSON recibido
    size_t total = json.iteratorBegin(); // Comienza a iterar sobre las claves

    Serial.println("Fechas encontradas:");
    for (size_t i = 0; i < total; i++) {
      int tipo;
      String clave, valor;
      json.iteratorGet(i, tipo, clave, valor); // Obtiene cada clave (fecha) individualmente
      fechas.push_back(clave);  //  Almacena la fecha en el vector
      Serial.println("📅 " + clave); // Imprime la fecha encontrada
    }

    json.iteratorEnd(); // Finaliza la iteración del JSON

    // Ordenar las fechas (alfabéticamente funciona por el formato YYYY-MM-DD)
    std::sort(fechas.begin(), fechas.end(), [](const String &a, const String &b) {
      return a.compareTo(b) < 0;
    });

    // Mostrar las fechas ordenadas
    Serial.println("📅 Fechas ordenadas:");
    for (size_t i = 0; i < fechas.size(); i++) {
      Serial.println("  → " + fechas[i]);
      String fecha = fechas[i];
      String path = ruta + "/" + fecha;
      // Obtiene el objeto JSON completo con las emociones para esa fecha
      if (Firebase.RTDB.getJSON(&fbdo, path)) {
        FirebaseJson& emociones = fbdo.to<FirebaseJson>();

        FirebaseJsonData ale, cal, can, est, ir, tri;
        // Extrae los valores emocionales individuales del JSON
        emociones.get(ale, "Alegría");
        emociones.get(cal, "Calma");
        emociones.get(can, "Cansancio");
        emociones.get(est, "Estrés");
        emociones.get(ir, "Ira");
        emociones.get(tri, "Tristeza");

        // Almacena los valores en los vectores globales
        v_alegria.push_back(ale.to<float>());
        v_calma.push_back(cal.to<float>());
        v_cansancio.push_back(can.to<float>());
        v_estres.push_back(est.to<float>());
        v_ira.push_back(ir.to<float>());
        v_tristeza.push_back(tri.to<float>());
        // Imprime los datos obtenidos de la fecha actual
        Serial.println("Fecha: " + fecha);
        Serial.println("  Alegría: " + ale.to<String>());
        Serial.println("  Calma: " + cal.to<String>());
        Serial.println("  Cansancio: " + can.to<String>());
        Serial.println("  Estrés: " + est.to<String>());
        Serial.println("  Ira: " + ir.to<String>());
        Serial.println("  Tristeza: " + tri.to<String>());
        Serial.println("-----------------------------------");

      } else {
        Serial.println("Error leyendo emociones de " + fecha + ": " + fbdo.errorReason());
      }
    }
    

  } else {
    Serial.println("Error al obtener las fechas: " + fbdo.errorReason());
  }
  // Imprime en consola todos los vectores emoción por emoción
  for (size_t i = 0; i < fechas.size(); i++) {
    Serial.print(fechas[i]); Serial.print("\t");

    Serial.print(v_alegria[i]); Serial.print("\t");
    Serial.print(v_calma[i]); Serial.print("\t");
    Serial.print(v_cansancio[i]); Serial.print("\t\t");
    Serial.print(v_estres[i]); Serial.print("\t");
    Serial.print(v_ira[i]); Serial.print("\t");
    Serial.println(v_tristeza[i]);
  }
}



//AQUI SE CALCULA EL PROMEDIO
float calcularPromedio(const std::vector<float>& vec) {
  float suma = 0;
  for (float v : vec) suma += v;
  return (vec.size() > 0) ? suma / vec.size() : 0;
}



Grados clasificarDifusamente(float x) {
  Grados g = {0, 0, 0};

  // Pertenencia a "bajo"
  if (x <= 0.0) g.bajo = 1.0;
  else if (x > 0.0 && x <= 2.0) g.bajo = (x - 0.0) / 2.0;
  else if (x > 2.0 && x <= 4.0) g.bajo = (4.0 - x) / 2.0;

  // Pertenencia a "medio"
  if (x == 5.0) g.medio = 1.0;
  else if (x > 3.0 && x < 5.0) g.medio = (x - 3.0) / 2.0;
  else if (x > 5.0 && x < 7.0) g.medio = (7.0 - x) / 2.0;

  // Pertenencia a "alto"
  if (x == 8.0) g.alto = 1.0;
  else if (x > 6.0 && x < 8.0) g.alto = (x - 6.0) / 2.0;
  else if (x > 8.0 && x < 10.0) g.alto = (10.0 - x) / 2.0;
  else if (x >= 10.0) g.alto = 1.0;

  return g;
}


//ESTA FUNCION MUESTRE EL VALOR CLASIFICADO DIFUSAMENTE
void mostrarValorClasificado(String nombre, float valor) {
  Grados nivel = clasificarDifusamente(valor);
  
  Serial.print("  ");
  Serial.print(nombre);
  Serial.print(": ");
  Serial.print(String(valor, 2));
  Serial.print(" → bajo: ");
  Serial.print(nivel.bajo, 2);
  Serial.print(", medio: ");
  Serial.print(nivel.medio, 2);
  Serial.print(", alto: ");
  Serial.println(nivel.alto, 2);
}



//ANALIZA EL ESTADO MENSUAL
void analizarEstadoMensual() {
  float prom_alegria = calcularPromedio(v_alegria);
  float prom_calma = calcularPromedio(v_calma);
   float prom_cansancio = calcularPromedio(v_cansancio);
  float prom_estres = calcularPromedio(v_estres);
  float prom_ira = calcularPromedio(v_ira);
  float prom_tristeza = calcularPromedio(v_tristeza);

  
  Serial.println("PROMEDIOS MENSUALES:");
  mostrarValorClasificado("Alegría", prom_alegria);
  mostrarValorClasificado("Calma", prom_calma);
  mostrarValorClasificado("Cansancio", prom_cansancio);
  mostrarValorClasificado("Estrés", prom_estres);
  mostrarValorClasificado("Ira", prom_ira);
  mostrarValorClasificado("Tristeza", prom_tristeza);
  Serial.println();

  retroalimentacion_mensual = retroalimentacion(
  clasificarDifusamente(prom_alegria),
  clasificarDifusamente(prom_calma),
  clasificarDifusamente(prom_cansancio),
  clasificarDifusamente(prom_estres),
  clasificarDifusamente(prom_ira),
  clasificarDifusamente(prom_tristeza)
  );
  
  retroalimentacion_mensual = "\\\"" + retroalimentacion_mensual + "\\\"" ;
  Serial.println(retroalimentacion_mensual);
  
  retroalimentacion_mensual_ansiedad = retroalimentacionAnsiedad(
  clasificarDifusamente(prom_alegria),
  clasificarDifusamente(prom_calma),
  clasificarDifusamente(prom_cansancio),
  clasificarDifusamente(prom_estres),
  clasificarDifusamente(prom_ira),
  clasificarDifusamente(prom_tristeza)
  );
  retroalimentacion_mensual_ansiedad = "\\\"" + retroalimentacion_mensual_ansiedad + "\\\"" ;
  Serial.println(retroalimentacion_mensual_ansiedad);

  retroalimentacion_mensual_depresion = retroalimentacionDepresion(
  clasificarDifusamente(prom_alegria),
  clasificarDifusamente(prom_calma),
  clasificarDifusamente(prom_cansancio),
  clasificarDifusamente(prom_estres),
  clasificarDifusamente(prom_tristeza)
  );

  retroalimentacion_mensual_depresion = "\\\"" + retroalimentacion_mensual_depresion + "\\\"" ;
  Serial.println(retroalimentacion_mensual_depresion);

}



//ANALIZA LA ULTIMA SEMANA
void analizarUltimaSemana() {
  int total = v_alegria.size();  // suponemos que todos los vectores tienen el mismo tamaño
  if (total < 7) {
    Serial.println("No hay suficientes datos para analizar la última semana.");
    return;
  }

  int inicio = total - 7;
  int fin = total;

  float a = promedioParcial(v_alegria, inicio, fin);
  float c = promedioParcial(v_calma, inicio, fin);
  float can = promedioParcial(v_cansancio, inicio, fin);
  float e = promedioParcial(v_estres, inicio, fin);
  float i = promedioParcial(v_ira, inicio, fin);
  float t = promedioParcial(v_tristeza, inicio, fin);

  Serial.println("Análisis de los últimos 7 días:");
  mostrarValorClasificado("Alegría", a);
  mostrarValorClasificado("Calma", c);
  mostrarValorClasificado("Cansancio", can);
  mostrarValorClasificado("Estrés", e);
  mostrarValorClasificado("Ira", i);
  mostrarValorClasificado("Tristeza", t);
  Serial.println();

  retroalimentacion_semanal = retroalimentacion(
    clasificarDifusamente(a),
    clasificarDifusamente(c),
    clasificarDifusamente(can),
    clasificarDifusamente(e),
    clasificarDifusamente(i),
    clasificarDifusamente(t)
  );
  retroalimentacion_semanal = "\\\"" + retroalimentacion_semanal + "\\\"" ;
  Serial.println(retroalimentacion_semanal);

  retroalimentacion_semanal_ansiedad = retroalimentacionAnsiedad(
    clasificarDifusamente(a),
    clasificarDifusamente(c),
    clasificarDifusamente(can),
    clasificarDifusamente(e),
    clasificarDifusamente(i),
    clasificarDifusamente(t)
  );
  retroalimentacion_semanal_ansiedad = "\\\"" + retroalimentacion_semanal_ansiedad + "\\\"" ;
  Serial.println(retroalimentacion_semanal_ansiedad);

  retroalimentacion_semanal_depresion = retroalimentacionDepresion(
    clasificarDifusamente(a),
    clasificarDifusamente(c),
    clasificarDifusamente(can),
    clasificarDifusamente(e),
    clasificarDifusamente(t)
  );
  retroalimentacion_semanal_depresion = "\\\"" + retroalimentacion_semanal_depresion + "\\\"" ;
  Serial.println(retroalimentacion_semanal_depresion);

}

//CALCULA EL PROMEDIDO SEMANAL
float promedioParcial(const std::vector<float>& vec, int inicio, int fin) {
  float suma = 0;
  for (int i = inicio; i < fin; i++) {
    suma += vec[i];
  }
  return (fin - inicio > 0) ? suma / (fin - inicio) : 0;
}

void analizarUltimoDia() {
  int total = v_alegria.size();  // asumimos todos los vectores tienen la misma longitud
  if (total == 0) {
    Serial.println("No hay datos disponibles para analizar el último día.");
    return;
  }

  float a = v_alegria[total - 1];
  float c = v_calma[total - 1];
  float can = v_cansancio[total - 1];
  float e = v_estres[total - 1];
  float i = v_ira[total - 1];
  float t = v_tristeza[total - 1];

  Serial.println("Análisis del último día registrado:");
  mostrarValorClasificado("Alegría", a);
  mostrarValorClasificado("Calma", c);
  mostrarValorClasificado("Cansancio", can);
  mostrarValorClasificado("Estrés", e);
  mostrarValorClasificado("Ira", i);
  mostrarValorClasificado("Tristeza", t);
  Serial.println();

  retroalimentacion_diaria = retroalimentacion(
    clasificarDifusamente(a),
    clasificarDifusamente(c),
    clasificarDifusamente(can),
    clasificarDifusamente(e),
    clasificarDifusamente(i),
    clasificarDifusamente(t)
  );
  retroalimentacion_diaria = "\\\"" + retroalimentacion_diaria + "\\\"" ;
  Serial.println(retroalimentacion_diaria);

  mensajeFisiologico = generarRetroalimentacionFisiologica(promedioFinal, hrv, spo2,clasificarDifusamente(a),clasificarDifusamente(c),
      clasificarDifusamente(can),clasificarDifusamente(e),clasificarDifusamente(i),clasificarDifusamente(t));
  Serial.println(mensajeFisiologico);
}



//ANALIZA EL ESTADO GENERAL
String retroalimentacion(
  Grados alegria, Grados calma, Grados cansancio,
  Grados estres, Grados ira, Grados tristeza
) {
  struct Opcion {
    float peso;
    String mensaje;
  };

  std::vector<Opcion> opciones;

  // Combinaciones relevantes
  opciones.push_back({alegria.alto + calma.alto + cansancio.bajo + estres.bajo + ira.bajo + tristeza.bajo, 
    "😊 Estás en equilibrio emocional. Mantén tus hábitos positivos como caminar al aire libre o hablar con personas queridas."});

  opciones.push_back({alegria.alto + calma.medio + cansancio.bajo, 
    "🌤 Buen estado general. Aprovecha tu energía para hacer algo creativo o productivo que te motive."});

  opciones.push_back({alegria.medio + calma.alto + cansancio.bajo, 
    "🙂 Estado emocional estable. Dedica tiempo a algo que disfrutes como leer o escuchar música."});

  opciones.push_back({alegria.bajo + calma.alto + tristeza.medio, 
    "🌧 Aunque estás tranquilo, tu ánimo está algo bajo. Sal a caminar o habla con alguien cercano."});

  opciones.push_back({alegria.bajo + cansancio.alto + estres.alto, 
    "😩 Cansancio y estrés detectados. Tómate una pausa, haz respiraciones profundas y duerme bien esta noche."});

  opciones.push_back({estres.alto + ira.alto, 
    "⚠️ Emoción intensa detectada. Aléjate del estímulo, respira 5 veces profundamente y realiza actividad física suave."});

  opciones.push_back({ira.medio + tristeza.medio, 
    "😕 Emociones mixtas. Intenta escribir lo que sientes o hacer algo que te relaje como colorear o escuchar música instrumental."});

  opciones.push_back({alegria.medio + cansancio.medio + estres.medio, 
    "💡 Tu estado es funcional pero puedes mejorar. Intenta planificar descansos y reducir multitareas."});

  opciones.push_back({alegria.bajo + calma.bajo + tristeza.alto, 
    "😔 Ánimo bajo. Escucha una canción que te guste, ve un vídeo gracioso o llama a alguien que te haga sentir bien."});

  opciones.push_back({calma.bajo + estres.medio + cansancio.medio, 
    "📈 Tensión creciente. Haz una pausa de 10 minutos, respira hondo y aléjate de pantallas por un rato."});

  opciones.push_back({ira.alto + alegria.bajo, 
    "🔥 Posible frustración acumulada. Da un paseo rápido, escribe lo que sientes y evita decisiones impulsivas."});

  opciones.push_back({alegria.bajo + tristeza.medio + calma.medio, 
    "🌀 Estado emocional sensible. Haz algo reconfortante como tomar una bebida caliente o darte una ducha relajante."});

  opciones.push_back({alegria.alto + cansancio.alto, 
    "⚖️ Estás animado pero físicamente cansado. Realiza estiramientos o una siesta corta (20 minutos)."});

  opciones.push_back({alegria.alto + calma.alto + cansancio.medio, 
    "😊 Muy buen estado general. Aprovecha para socializar o reforzar un hábito positivo que hayas iniciado."});

  opciones.push_back({cansancio.medio + estres.alto + calma.bajo, 
    "😓 Puedes estar al límite. Cierra los ojos por un par de minutos, reduce exigencias y prioriza tu salud."});

  opciones.push_back({alegria.bajo + estres.medio + tristeza.medio, 
    "🌫 Ánimo inestable. Intenta actividades como journaling o realizar pequeños logros que te den satisfacción."});

  opciones.push_back({cansancio.alto + alegria.medio, 
    "😪 Fatiga marcada. Intenta desconectarte del trabajo o estudio hoy. Necesitas recargar."});

  opciones.push_back({calma.alto + estres.bajo + tristeza.medio, 
    "🌿 Tranquilidad con algo de melancolía. Date un paseo al aire libre o medita por 5 minutos."});

  // Valor por defecto
  opciones.push_back({0.01, "📋 Estado emocional dentro de parámetros aceptables. Sigue cuidándote y monitoreando tu bienestar."});

  // Elegir la mejor opción
  Opcion mejor = opciones[0];
  for (auto op : opciones) {
    if (op.peso > mejor.peso) mejor = op;
  }

  return mejor.mensaje;
}



//SE CONECTA A FIREBASE PARA SUBIR LA RETROALIMENTACION
void subirRetroalimentacion(String tipo, String clave, String mensaje) {
  String path = "/Usuarios/Pablo/Retroalimentacion/" + tipo + "/" + clave;
  if (Firebase.RTDB.setString(&fbdo, path, mensaje)) {
    Serial.println("Retroalimentación " + tipo + " guardada en: " + path);
  } else {
    Serial.println("Error al subir " + tipo + ": " + fbdo.errorReason());
  }
}

//OBTIENE LA FECHA DEL DIA DE HOY
String obtenerFechaYYYYMMDD() {
  struct tm timeinfo;
  if (!getLocalTime(&timeinfo)) return "fecha-desconocida";

  char buffer[11];
  strftime(buffer, sizeof(buffer), "%Y-%m-%d", &timeinfo);
  return String(buffer);
}


//ANALIZA LA POSIBILIDAD DE ANSIEDAD
String retroalimentacionAnsiedad(
  Grados alegria, Grados calma, Grados cansancio,
  Grados estres, Grados ira, Grados tristeza
) {
  struct Opcion {
    float peso;
    String mensaje;
  };

  std::vector<Opcion> opciones;

  opciones.push_back({estres.alto + calma.bajo + ira.alto, 
    "Riesgo de ansiedad alto: tensión elevada y poca calma. Haz respiraciones profundas 4-7-8, reduce estímulos externos y descansa en un entorno tranquilo."});

  opciones.push_back({estres.medio + calma.bajo + ira.medio, 
    "Riesgo de ansiedad moderado: podrías estar acumulando estrés. Haz pausas activas, camina 10 minutos o escucha música relajante."});

  opciones.push_back({estres.alto + alegria.bajo + calma.medio, 
    "Ansiedad encubierta: podrías no estar expresando tus emociones. Intenta escribir lo que sientes o hablar con alguien de confianza."});

  opciones.push_back({alegria.alto + calma.alto + estres.bajo + ira.bajo, 
    "Riesgo de ansiedad muy bajo: sigue con tus rutinas de autocuidado y equilibrio emocional. Reforzar hábitos como meditar o caminar te ayudará a mantener este estado."});

  opciones.push_back({(alegria.medio + alegria.alto)/2 + tristeza.bajo + estres.bajo, 
    "Recuperación emocional en curso: continúa haciendo actividades que te motiven y evita la sobrecarga de tareas."});

  opciones.push_back({alegria.alto + cansancio.bajo + tristeza.bajo, 
    "Alta energía emocional y bajo riesgo de ansiedad. Aprovecha para emprender algo nuevo o socializar con personas positivas."});

  opciones.push_back({calma.alto + estres.bajo + ira.bajo, 
    "Paz emocional detectada. Sigue alimentando tu tranquilidad con rutinas estables y tiempo para ti."});

  opciones.push_back({cansancio.medio + alegria.medio + estres.medio, 
    "Avances sostenidos en el manejo emocional. Refuerza lo que te está funcionando y cuida tu descanso."});

  opciones.push_back({estres.medio + ira.bajo + alegria.bajo, 
    "Riesgo leve de ansiedad anticipatoria. Practica mindfulness y limita la exposición a fuentes de preocupación innecesarias."});

  opciones.push_back({estres.alto + ira.bajo + tristeza.medio, 
    "Tensión emocional acumulada. Un baño caliente, respiraciones conscientes o una caminata pueden ayudarte a regularte."});

  opciones.push_back({alegria.bajo + estres.medio + calma.medio, 
    "Posible confusión emocional. Evita decisiones importantes hoy y procura dormir bien esta noche."});

  opciones.push_back({0.01, 
    "No se notifican indicios de ansiedad. Mantén tu bienestar y monitorea regularmente tus emociones."});

  Opcion mejor = opciones[0];
  for (auto op : opciones) {
    if (op.peso > mejor.peso) mejor = op;
  }

  return mejor.mensaje;
}





//ANALIZA LA POSIBILIDAD DE DEPRESION
String retroalimentacionDepresion(
  Grados alegria, Grados calma, Grados cansancio,
  Grados estres, Grados tristeza
) {
  struct Opcion {
    float peso;
    String mensaje;
  };

  std::vector<Opcion> opciones;

  opciones.push_back({alegria.bajo + tristeza.alto + cansancio.alto,
    "Riesgo de depresión alto: desánimo y fatiga notables. Te recomendamos hablar con alguien de confianza y realizar una actividad ligera como caminar o escuchar música suave."});

  opciones.push_back({alegria.bajo + tristeza.medio + calma.bajo,
    "Riesgo de depresión moderado: bajo ánimo y dificultad para relajarte. Realiza pequeñas actividades placenteras, evita el aislamiento y considera escribir lo que sientes."});

  opciones.push_back({alegria.bajo + tristeza.alto + estres.medio,
    "Señales de posible depresión con estrés acumulado. Intenta reducir tus exigencias, duerme bien y dedica tiempo a una sola actividad sencilla que te guste."});

  opciones.push_back({tristeza.alto + alegria.bajo,
    "Tristeza prolongada sin motivación. No estás solo: intenta abrirte con alguien cercano o contactar con apoyo psicológico. Un paseo corto también puede ayudar."});

  opciones.push_back({alegria.medio + tristeza.medio + cansancio.medio,
    "Riesgo de depresión leve: cuida tus rutinas de sueño, aliméntate bien y dedica al menos 15 minutos al día a algo que disfrutes (lectura, música, etc.)."});

  opciones.push_back({alegria.bajo + tristeza.medio + calma.medio,
    "Posible inicio de apatía emocional. Prueba cambiar de entorno, caminar en la naturaleza o realizar tareas breves que den sensación de logro."});

  opciones.push_back({alegria.alto + tristeza.medio + cansancio.bajo,
    "Buen estado general con leve tristeza. Aprovecha tu motivación actual para mantener contacto social y realizar actividades reconfortantes."});

  opciones.push_back({estres.bajo + tristeza.alto + alegria.medio,
    "Tristeza emocional sin sobrecarga de estrés. Busca espacios para expresar lo que sientes, ya sea hablando o escribiendo. Mantente activo/a."});

  opciones.push_back({cansancio.alto + alegria.bajo + calma.medio,
    "Riesgo emocional: la energía está bloqueada. Intenta moverte un poco, aunque sea dentro de casa, y limita la exposición a noticias o redes sociales."});

  opciones.push_back({alegria.alto + tristeza.bajo + cansancio.bajo,
    "Riesgo de depresión muy bajo: ánimo elevado y buena energía. Sigue cuidando tu salud emocional con descanso, movimiento y vínculos positivos."});

  opciones.push_back({alegria.medio + tristeza.bajo + estres.bajo,
    "Riesgo muy bajo de depresión: emocionalmente estable. Reforzar este estado con hábitos como leer, pasear o cuidar plantas es ideal."});

  opciones.push_back({0.01,
    "Riesgo de depresión muy bajo. Continúa cuidando tu salud emocional con rutinas saludables y conexión social."});

  Opcion mejor = opciones[0];
  for (auto op : opciones) {
    if (op.peso > mejor.peso) mejor = op;
  }

  return mejor.mensaje;
}



float calcularHRV(const std::vector<long>& rr) {
  if (rr.size() < 2) return 0;

  // Calcular media
  float suma = 0;
  for (long valor : rr) suma += valor;
  float media = suma / rr.size();

  // Calcular varianza
  float sumaCuadrados = 0;
  for (long valor : rr) {
    sumaCuadrados += pow(valor - media, 2);
  }

  // Desviación estándar = HRV estimado
  float desviacion = sqrt(sumaCuadrados / (rr.size() - 1));

  // Acotar HRV a un rango fisiológico razonable (20-150 ms)
  desviacion = constrain(desviacion, 20, 150);

  return desviacion;
}


std::vector<long> filtrarRR(const std::vector<long>& rr) {
  std::vector<long> filtrado;
  for (long val : rr) {
    if (val >= 400 && val <= 1200) {  // más restrictivo
      filtrado.push_back(val);
    }
  }
  return filtrado;
}

String generarRetroalimentacionFisiologica(
  float promedioBPM, float hrv, float spo2,
  Grados alegria, Grados calma, Grados cansancio,
  Grados estres, Grados ira, Grados tristeza
) {
  struct Opcion {
    float peso;
    String mensaje;
  };

  std::vector<Opcion> opciones;

  // Clasificación difusa para BPM
  float bpm_bajo = (promedioBPM < 50) ? 1.0 : 0.0;
  float bpm_normal = (promedioBPM >= 50 && promedioBPM <= 90) ? 1.0 : 0.0;
  float bpm_alto = (promedioBPM > 90) ? 1.0 : 0.0;

  // Clasificación difusa para HRV
  float hrv_bajo = (hrv < 40) ? 1.0 : 0.0;
  float hrv_moderado = (hrv >= 40 && hrv <= 100) ? 1.0 : 0.0;
  float hrv_alto = (hrv > 100) ? 1.0 : 0.0;

  // Clasificación difusa para SpO2
  float spo2_bajo = (spo2 < 90) ? 1.0 : 0.0;
  float spo2_moderado = (spo2 >= 90 && spo2 < 95) ? 1.0 : 0.0;
  float spo2_normal = (spo2 >= 95) ? 1.0 : 0.0;

  // Reglas combinadas con pesos
  opciones.push_back({hrv_alto + bpm_normal + spo2_normal + alegria.alto + calma.alto + estres.bajo,
    "Excelente estado físico y emocional. Mantén tus rutinas de bienestar y sigue disfrutando de tus días."});

  opciones.push_back({hrv_bajo + bpm_alto + estres.alto + ira.alto,
    "Estrés elevado detectado: ritmo cardíaco alto, baja variabilidad y emociones intensas. Tómate una pausa, respira profundo y considera hablar con alguien."});

  opciones.push_back({spo2_bajo + cansancio.alto + tristeza.medio,
    "Bajo oxígeno y señales de fatiga. Asegúrate de descansar bien, salir al aire libre y cuidar tu respiración."});

  opciones.push_back({hrv_moderado + bpm_normal + spo2_normal + alegria.medio + calma.medio,
    "Buen estado general. Refuerza lo positivo con actividades agradables y momentos de tranquilidad."});

  opciones.push_back({hrv_bajo + estres.medio + cansancio.medio,
    "Tu cuerpo muestra signos de agotamiento. Intenta dormir mejor y reduce estímulos externos hoy."});

  opciones.push_back({spo2_normal + alegria.bajo + tristeza.alto,
    "Aunque tu oxigenación es buena, emocionalmente podrías estar baj@. Intenta conectar con alguien y hacer algo que te anime."});

  opciones.push_back({0.01,
    "Estado fisiológico aceptable. Vigila tus emociones y haz una pausa si sientes tensión o cansancio acumulado."});

  Opcion mejor = opciones[0];
  for (auto op : opciones) {
    if (op.peso > mejor.peso) mejor = op;
  }

  return "\\\"" + mejor.mensaje + "\\\"";
}
