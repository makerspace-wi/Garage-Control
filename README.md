# Garage-Control_B0.9.x
Entry Control for Makerspace Wiesbaden Garage Door

Momentan wird unsere Garage mit einem Schlüssel geöffnet und geschlossen. 
Oft wird der Schlüssel verlegt und muss gesucht werden, wenn was aus der Garage benötigt wird.
Desweiteren ist momentan nicht nachvollziehbar wer von den Mitgliedern Zugang zur Garage hatte.

Aus den Reihen der Mitglieder kam der Vorschlag auch den Garagenzugang mit RFID-Zugangsteuerung zu realisieren.

Dieser Vorschlag wurde vom Vorstand als gut befunden und soll in naher Zukunft für die existierende Infrastruktur durch das Technik-Team realisiert werden.

Im folgenden dazu unser "Brain Storming":

- XBee ZBee Funkreichweite wurde geprüft und ist ausreichend!
- Aussen RFID-Leser mit NOT STOP-Taste und Display (I2C Bus für RFID-Reader & Display, Spannungsversorgung, Tasterkontakte und LED) 
- benötigen wir ein Display? JA!
- Innen Arduino, Netzteil, XBee ZBee Modul und Relaisplatine (für Rolltorsteuerung)
- Das Steuerprogramm (Arduino) wird 'dumm' gehalten 
  - es sendet die gelesene UID <strike>und Tasterkontakte</strike> an Symcon zur Prüfung ob vorhanden und freigegeben
  - es reagiert auf Befehle von Symcon (z.B. x Sekunden AUF/ x Sekunden AB ......)
- es werden Rolltor Positionssensoren installiert und verwendet?

# Nutzung
RFID Chip wird an Lesegerät gehalten und die UID von Arduino empfangen - Arduino sendet die dekodierte UID an Symcon zur Prüfung
Wenn die UID freigeschaltet ist, sendet Symcon einen 'x Sekunden AUF' Befehl - ist die UID nicht freigeschaltet, verwirft Symcon den Request.<br>
Wird der RFID-Chip nochmals an das Lesegerät gehalten, reagiert Symcon mit einen 'x Sekunden AB' Befehl

Mit der NOT STOP Taste kann das Auf- und Zufahren gestoppt werden, falls sich versehentlich was im Führungsweg des Rolltors befindet.
