# Garage Door Entry Control for Makerspace Wiesbaden
<br>
<img src="https://github.com/makerspace-wi/Garage-Control/assets/42463588/5b9a2b25-91ed-47d1-92b1-6bd77940fd07" width="500px">
<br>

# Zusammenfassung:

- Kommunikation mit dem SYMCON Rechner via XBee ZBee Funktechnik
- Aussen RFID-Leser mit NOT STOP-Taste und Display (I2C Bus für RFID-Reader & Display, Spannungsversorgung, Tasterkontakte und LED) 
- Innen Arduino, Netzteil, XBee ZBee Modul und Relaisplatine (für Rolltorsteuerung)
- Das Steuerprogramm (Arduino) wird 'dumm' gehalten 
  - es sendet die gelesene UID und Tasterkontakte an Symcon zur Prüfung ob vorhanden und freigegeben
  - es reagiert auf Befehle von Symcon (z.B. 'OPEN' - 'CLOSE')
- es werden Rolltor Positionssensoren (Reed Kontakte) verwendet

# Nutzung
## ACHTUNG, lässt sich nur öffnen und schließen, wenn das Mitglied 'eingeloggt' ist
### Ausgangszustand Rolltor geschlossen

- Das Display vom Lesegerät ist deaktiviert
- RFID Chip an Lesegerät halten, das Display sollte einschalten und Nachrichten zeigen
- Wenn der Chip freigeschaltet ist, sendet Symcon einen 'OPEN' Befehl - das Tor öffnet sich
- ist der Chip nicht freigeschaltet oder das Mitglied nicht eingeloggt, verwirft Symcon die Öffnungsanfrage
- wenn das Rolltor die obere Position erreicht hat, wird dies im Display angezeigt und nach 15 Sekunden deaktiviert

### Ausgangszustand Rolltor geöffnet

- Das Display vom Lesegerät ist deaktiviert
- RFID Chip an Lesegerät halten, das Display sollte einschalten und Nachrichten zeigen
- Wenn der Chip freigeschaltet ist, sendet Symcon einen 'CLOSE' Befehl - das Tor schliesst sich
- ist der Chip nicht freigeschaltet oder das Mitglied nicht eingeloggt, verwirft Symcon die Schließanfrage
- wenn das Rolltor die untere Position erreicht hat, wird dies im Display angezeigt und nach 15 Sekunden deaktiviert

### NOT STOP Funktion

- Mit der <b>NOT STOP</b> Taste am Lesegerät kann das Auf- oder Zufahren sofort gestoppt werden, falls sich versehentlich was im Führungsweg des Rolltors befindet
- bei Betätigung der Taste bleibt das Rolltor sofort stehen und fährt erst nach nochmaligem präsentieren des RFID-Chips erst mal in die "Offen"-Position

### Was wird geloggt
- jede Öffnung und folgende Schließung wird in der Datenbank geloggt
- dabei wird
  - Öffnungs Datum/Zeit, die Mitglieds MID und der Name des Mitglieds und
  - Schließungs Datum/Zeit, die Mitglieds MID und der Name des Mitglieds und
  - wie lange die Garage geöffnet war
- die Daten werden nach 30 Tage gelöscht

<img src="https://github.com/makerspace-wi/Garage-Control/assets/42463588/8a614019-b9a1-47b2-84d5-a168b6a27a21" width="500px">
<img src="https://github.com/makerspace-wi/Garage-Control/assets/42463588/8235685b-468d-4010-9f8a-889b63d0cb86" width="500px">
<br>
<img src="https://github.com/makerspace-wi/Garage-Control/assets/42463588/66339dbe-f8c4-4da0-b39c-f666d4c72afb" width="500px">
<br>

<img src="https://github.com/makerspace-wi/Garage-Control/assets/42463588/5dbc76eb-141f-447e-8ef3-db277cf56024" width="500px">
<img src="https://github.com/makerspace-wi/Garage-Control/assets/42463588/d775f473-c52c-4a3c-be16-ce14e2962205" width="500px">

