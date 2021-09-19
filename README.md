# Garage-Control
Entry Control for Makerspace Wiesbaden Garage Door

Momentan wird unsere Garage mit einem Schlüssel geöffnet und geschlossen. 
Oft wird der Schlüssel verlegt und muss gesucht werden, wenn was aus der Garage benötigt wird.
Desweiteren ist momentan nicht nachvollziehbar wer von den Mitgliedern Zugang zur Garage hatte.

Aus den Reihen der Mitglieder kam der Vorschlag auch den Garagenzugang mit RFID-Zugangsteuerung zu realisieren.

Dieser Vorschlag wurde vom Vorstand als gut befunden und soll in naher Zukunft für die existierende Infrastruktur durch das Technik-Team realisiert werden.

Im folgenden dazu unser "Brain Storming":

- XBee ZBee Funkreichweite wurde geprüft und ist ausreichend!
- Aussen RFID-Leser mit Display und Abmeldetaste (I2C Bus für Display und RFID-Reader, Spannungsversorgung und Tasterkontakte und LED?)
- Innen Arduino, Netzteil, XBee ZBee Modul und Relaisplatine
