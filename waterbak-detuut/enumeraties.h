/****************************************************************************************************
  Dit bestand bevat hulp enumeraties welke gebruikt worden om de toestanden van de ingangen naar een
  stand van de schakelaar of vlotter te vertalen. Deze worden vooral gebruikt om de toestanden van 
  ingangen in de overgangen makkelijker te kunnen vertalen naar de echte wereld.

  Auteur: Edwin Spil
****************************************************************************************************/

// Enumeratie van de drie standen van de standenschaklaar
enum StandenSchakelaarStand { NUL, HAND, AUTO };

// Enumeratie van de twee standen van de vlotter
enum VlotterStand { TELAAG, OK };

// Defenitie van de functies met de enumeraties als retourwaarde
StandenSchakelaarStand getStandenSchakelaarStand();
VlotterStand getVlotterStand();