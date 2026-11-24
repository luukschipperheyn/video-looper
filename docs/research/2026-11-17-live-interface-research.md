# Onderzoek: Web interface voor liveperformance op iPad

Onderzoeksnotities, 17 november 2026. Context: een 13-inch iPad, liggend op tafel of standaard; een ontwerpvoorstel en visueel prototype. De performer noemt Overdub, Video selecteren, Blend, transformeren via het trackpad, Loop start en ADSR als belangrijke livehandelingen. De workflow is experimenteel en zal waarschijnlijk veranderen. Dit document beschrijft bronnen en ontwerphypothesen, geen vastgesteld implementatiespec. De projecttermen volgen [CONTEXT.md](../../CONTEXT.md).

## Conclusie voor het ontwerp

Behandel de Web interface als een bespeelbaar instrument. Maak de zes genoemde livehandelingen direct bereikbaar, met Overdub en de actuele toestand op een vaste plek. Houd Cue, Latch, Panic en Opacity nabij de Overdubbediening. Zet minder frequente instellingen in een tweede laag; FX is in deze prioritering secundair. Dit is onze ontwerpvertaling van het onderzoek en de productvoorbeelden hieronder; de precieze indeling moet met dit instrument op de iPad worden beproefd.

Er is onderzoek naar audio-interfaces, plugininterfaces en touchbediening. Dat levert geen universeel bewezen winnaar op tussen knobs, faders en XY-vlakken. Onderzoeken meten verschillende taken, vaak met kleine groepen en meestal buiten een live VJ-setting. Officiële productdocumentatie bewijst hoe een product werkt, niet dat het de beste of snelste oplossing is.

## Onderzoek: wat is daadwerkelijk aangetoond?

| Bron | Bevinding en bewijssoort | Betekenis en beperking voor deze looper |
|---|---|---|
| [Gelineck & Serafin — A Quantitative Evaluation of the Differences Between Knobs and Sliders, NIME 2009](https://nime.org/proceedings/2009/nime2009_013.pdf) | Experiment met 20 elektronische muzikanten, hardwarecontrollers en twee synthesemodellen. Geen significant verschil tussen knobs en sliders in de onderzochte gebruikersratings; de taakresultaten waren niet doorslaggevend. Voorkeuren verschilden. | Geen basis voor “faders zijn wetenschappelijk beter”. Kies op functie, beschikbare ruimte en gewenning. Hardwareweerstand en korte tests beperken overdracht naar een glazen iPad. |
| [Dewey & Wakefield — Formal usability evaluation of audio track widget graphical representation…, AES 2017](https://eprints.hud.ac.uk/id/eprint/32145/1/AES142_TrackVis2DSP_FINISHED.pdf) | 18 deelnemers met minimaal één jaar productie-ervaring selecteerden tracks via zeven desktopinterfaces. Bij 32 tracks was de eenvoudige tekstweergave het efficiëntst; gebruikers prefereerden iconen en de traditionele mixer. | Visuele voorkeur en snelheid apart beoordelen. Beknopte herkenbare labels behouden. Dit was een zoektaak met een muis, geen bewijs dat een VJ-videobrowser zonder thumbnails beter is. |
| [Clément & Bernardes — Assessing the Influence of Multimodal Feedback in Mobile-Based Musical Task Performance, 2022](https://www.mdpi.com/2414-4088/6/8/68) | 60 geldige deelnemers, Androidtelefoons, tonen reproduceren via verticale touchbeweging. Visuele feedback veranderde het fijnregelen, maar extra visuele/haptische feedback gaf geen significant algemeen voordeel in totale tijd en toonhoogtenauwkeurigheid. | Geef betekenisvolle feedback, maar presenteer meer animatie of haptiek niet als bewezen verbetering. De taak, apparaten en populatie verschillen sterk van ervaren VJ's. |
| [Vogel & Baudisch — Shift, CHI 2007; auteurspagina en paper](https://patrickbaudisch.com/projects/shift/index.html) | Onderzoek naar kleine doelen op pengeoriënteerde touchapparaten: de vinger bedekt het doel en maakt het selectiepunt onduidelijk. Een verplaatste weergave helpt bij kleine doelen; grote doelen kunnen direct blijven werken. | Zet een parameterwaarde boven of naast het bediende vlak, niet uitsluitend onder de vinger. Gebruik royale pads. De exacte resultaten voor PDA's zijn geen maatvoeringsnorm voor een moderne iPad. |
| [McGregor — Knobs and Nodes: a study of UI design in audio plugins, 2019](https://mro.massey.ac.nz/items/f630372b-5871-4864-b4ab-aefc49e4e1fc) | Masteronderzoek met twee nodegebaseerde delay-/reverbplugins, getest door producers, sounddesigners en componisten in hun eigen werkomgeving. | Direct bewijs dat alternatieve plugininterfaces zijn onderzocht. Alleen repositorymetadata en abstract waren bereikbaar; het PDF-document liep vast. Daarom hier geen claims over steekproefgrootte, effectgrootte of een winnende interface. |
| [Bell, Hein & Ratcliffe — Beyond Skeuomorphism, 2015](https://www.arpjournal.com/asarpwp/beyond-skeuomorphism-the-evolution-of-music-production-software-user-interface-metaphors-2/) | Historische en conceptuele analyse van audiotechnologie en interfacemetaforen. Hardwaremetaforen kunnen helpen bij herkenning, maar leggen ook beperkingen op wanneer digitale mogelijkheden anders zijn. | Neem functionele ordening en herkenbare controls over uit VST's. Een metalen frontplaat, decoratieve schroeven en kleine draaiknoppen zijn geen vereiste. Dit artikel is geen gecontroleerde vergelijking van flat versus realistische vormgeving. |

## Officiële productpatronen om van te leren

| Product en primaire bron | Geobserveerd patroon | Passende vertaling |
|---|---|---|
| [FabFilter Pro-Q 4: interfaceoverzicht](https://www.fabfilter.com/help/pro-q/using/overview) | Eén grote interactieve visualisatie; details verschijnen bij de geselecteerde band; algemene functies hebben een vaste balk. | Een leesbare envelopcurve bij ADSR en een duidelijke Loop-positie. Details contextualiseren. De belangrijkste performancecontrols blijven steeds beschikbaar; niet alles achter selectie verstoppen. |
| [FabFilter Pro-Q 4: iPadbediening](https://www.fabfilter.com/help/pro-q/using/ios) | Knobs worden met een verticale drag bediend. Een tweede vinger geeft fijnregeling; dubbel tikken herstelt de standaardwaarde. | Een knob hoeft geen cirkelgebaar te vragen. Voor veelgebruikte Opacity kan een brede fader duidelijker zijn. Gebruik herkenbare enkelvoudige gebaren; ingewikkelde shortcuts alleen aanvullend. |
| [Resolume: clips en triggerstijlen](https://resolume.com/support/en/clips) | Thumbnails activeren media; selecteren om instellingen te veranderen is een aparte actie. Normal, Toggle en Piano hebben expliciet verschillend gedrag. Previewen en activeren worden onderscheiden. | Duidelijk maken welke Video de Source is en welke actie de Loop verandert. Maak Overdub vasthouden en Latch schakelen zichtbaar verschillend. Neem geen extra lagen, decks of A/B-mixer over: dit project heeft één Loop. |
| [VDMX: Control Surface en Media Bin](https://docs.vidvox.net/vdmx/vdmx_plugins.html) | VDMX laat een beperkte set mastercontrols samenstellen, met eigen labels en indeling. Het kent momentary/toggle, 2D-posities, sliders en een Web interface die teruggekoppelde waarden kan tonen. | Ontwerp één doelgericht performanceoppervlak rond de werkelijk gebruikte handelingen. Bedieningswaarden moeten overeenkomen met de toestand op de Mac, ook na wijzigen via een andere controller. Een volledige layout-editor is daarvoor niet nodig. |
| [TouchOSC: Control Reference](https://hexler.net/touchosc/manual/controls) | Afzonderlijke momentary- en togglebuttons; faders en XY-vlakken kunnen absoluut of relatief reageren. Bij XY kunnen assen worden vastgezet. | Gebruik verschillende controltypen voor vasthouden, schakelen en continu veranderen. Relatieve drag kan een onbedoelde waardesprong voorkomen. Een XY-vlak is nuttig voor bewust gekoppelde parameters, niet automatisch voor ieder willekeurig parameterpaar. |
| [Apple Human Interface Guidelines: Accessibility](https://developer.apple.com/design/human-interface-guidelines/accessibility) ([leesbare bronversie](https://developer.apple.com/design/human-interface-guidelines/accessibility.md)) | De actuele tabel noemt 44×44 pt als standaardmaat en 28×28 pt als minimum voor iPadOS. Verder: tussenruimte, eenvoudige gebaren, alternatieven voor gestures en betekenis niet alleen via kleur. | Voor deze livebediening is onze voorstelmaat royaler: gewone doelen circa 56–64 CSS px, performancepads duidelijk groter, onderlinge ruimte en tekstuele status. Dit zijn ontwerpkeuzes om op het apparaat te testen; native punten en CSS-pixels zijn geen universeel uitwisselbare fysieke maten. |

## Grenzen die het ontwerp correct moet uitleggen

De onderstaande betekenis komt uit de projectdocumentatie, niet uit Resolume of een andere VJ-app:

- **Cue toont de Audition aan het publiek zonder in de Loop te schrijven.** Cue is hier geen verborgen, privébeeld zoals een VJ-previewmonitor. De interface moet dit verschil expliciet houden.
- **Latch start onmiddellijk Overdub.** Het is geen wachtstand. Het label en de actieve toestand moeten dat leesbaar maken.
- **Panic geeft Latch, latched Cue en vastgehouden gates vrij.** Noem dit niet “Clear”, “Reset” of “Blackout”.
- **Bake FX verandert wat naar de Loop wordt geschreven.** Dit verdient een onderscheidbare toestand ten opzichte van alleen FX aanzetten.
- **Een Video kiezen, Overdub en Commit zijn verschillende acties.** Commit schrijft een willekeurige volledige Video, op volle opacity en buiten ADSR om.

## Ontwerphypothesen voor het visuele prototype

1. **Vaste performancezone:** behoud de sterke bestaande basis van altijd zichtbare Overdub/Cue/Panic. Breng Opacity, Latch en schrijfstatus in dezelfde directe omgeving. Houd Blend, Source-transform, Loop start en ADSR direct bereikbaar in de voorkeursvariant. Houd de vaste controls op hun plek wanneer een detailpaneel opent.
2. **Kleine bibliotheek naast grote bediening:** toon een bruikbare rij of compact raster Videos met duidelijke selectie; meer Videos kunnen op een eigen pagina/paneel. De volledige bibliotheek hoeft niet het eerste scherm te domineren.
3. **Categorische keuzes als keuzes:** Blend is een benoemde mode, geen continue grootheid. Een herkenbare selectie van modi, met toegang tot alle modi, is een kandidaatverbetering ten opzichte van een schuif die door namen loopt.
4. **Toestand op de plek van de actie:** laat Latch aan/uit en Overdub actief/vrijgegeven niet alleen door een kleurverschil zien. Toon ook tekst, een vaste markering en de Envelope-toestand. Houd waarden buiten het contactvlak van de vinger.
5. **Functionele feedback en transformruimte:** geef het bestaande Source-trackpad voldoende ruimte voor verplaatsen, schalen en roteren. Plaats de bijbehorende waarden buiten het vlak. Loop-positie en envelop mogen bewegen wanneer dat gedrag uitlegt. Voorkom animatie als decoratie en een voortdurend veranderende indeling. Een fysieke knop voelen of volledig blind bedienen is niet door vormgeving alleen na te bootsen.
6. **Setup apart:** Export, fps, webcamindex, technische verbindinginstellingen en zelden gebruikte Source-opties zijn kandidaat voor een secundair paneel. Het prototype moet duidelijk aangeven wanneer een bediening alleen gesimuleerd wordt.

Deze hypothesen zijn geen aanleiding om nu de werkende applicatie of het Loop-model te wijzigen.

## Hoe de kandidaatindeling later eerlijk te vergelijken

Vergelijk huidige interface en prototype op dezelfde 13-inch iPad, in dezelfde stand, met dezelfde Videos. Gebruik een korte oefenronde en wissel de volgorde van de ontwerpen. Laat de performer een Video kiezen, Blend wijzigen, de Source transformeren, Loop start verplaatsen en ADSR aanpassen terwijl Overdub beschikbaar blijft. Voeg Cue vasthouden, Latch vrijgeven en Panic vanuit een detailpaneel toe. Bekijk taakduur, verkeerde aanrakingen, onbedoelde waardesprongen, benodigde scrollbewegingen en hoe vaak de blik van Program naar de bediening moet. Vraag daarnaast afzonderlijk naar comfort en vertrouwen.

Een korte test kan bedieningsproblemen vinden, maar onderbouwt nog geen claims over betrouwbaarheid of vermoeidheid tijdens een volledige show. Een langere repetitie, met gelijktijdige aanrakingen en de echte netwerkverbinding, blijft nodig voordat de gekozen vorm productiewaardig heet.

## Inspectie van de huidige Web interface

De draaiende interface op `192.168.1.76:8734` is op 17 november bekeken, zonder bediening van de echte performancecontrols. De bevindingen zijn vergeleken met [de webpagina](../../app/web/index.html), [het controlschema](../../app/src/remote/ControlSchema.hpp), [de terugkoppeling](../../app/src/remote/RemoteApply.cpp) en [de frameverwerking](../../app/src/App.cpp).

- Het scherm begint met verbinding, Export, een tekstregel met Loop/Envelope/gates en maximaal twaalf Videos. In de geobserveerde layout van 1085 × 758 CSS px begon de sectie Overdub rond y=753, ADSR rond y=1058 en FX rond y=1355. Dit zijn browsermetingen, geen meting op de fysieke iPad. De belangrijkste parameters vragen dus scrollen terwijl de vaste pads wel bereikbaar zijn.
- De vaste Overdub-, Cue- en Panic-zone is een goede bestaande basis. Een redesign moet deze eigenschap behouden, ook tijdens instellingen.
- Blend is momenteel een slider door twaalf categorieën met kleine namen. Directe knoppen maken een bepaalde mode doelgericht bereikbaar. Door modi heen vegen kan muzikaal nuttig zijn; een eventuele sweepbediening moet daarom bewust gekozen worden en apart getest worden.
- Het Source-trackpad staat diep in de Source-instellingen en toont geen transformgeometrie of numerieke toestand. Een zichtbaar Source-kader en waarden buiten het contactvlak maken het gebaar beter leesbaar, zonder een videostream te vereisen.
- De webpagina krijgt thumbnails en numerieke snapshots. Er is in de onderzochte code geen live Program- of Audition-stream naar de browser. Een echte videovoorvertoning op de iPad is dus apart ontwikkelwerk, met een te meten performancekost.
- `Param 1/2/3` zijn geen bruikbare performancelabels. [FxPipeline.cpp](../../app/src/gl/FxPipeline.cpp) bepaalt hun betekenis per mode. Bij Feedback zijn het mix, decay en zoom; bij Difference is parameter 1 ongebruikt. Bij Blur wordt parameter 1 zowel voor feedbackmix als voor de blurradius gebruikt. Alleen hernoemen naar “Radius” zou daar gedrag verbergen.
- Panic geeft gates en latches vrij; de ADSR-release kan nog uitlopen. Het is geen onmiddellijke blackout of Undo. Na verbindingsverlies zijn vrijgegeven momentary pads en de toestand van Latch verschillende zaken: lokaal geen aanraking meer zien bewijst niet dat de Mac geen Latch meer heeft. Toon onbekende status en de Mac als terugval, geen onbevestigde “veilig/uit”-melding.

## De drie visuele kandidaten

Het interactieve ontwerp staat in deze Codex-chat. Het is een lokale demonstratie met schematische thumbnails, een statische voorbeeldpositie van de Loop en vereenvoudigde Envelope-overgangen. Het verstuurt geen opdrachten naar de looper.

| Kandidaat | Verdeling | Afweging |
|---|---|---|
| **Instrument — voorlopige voorkeur** | Links zes Videos en Video loop start. Rechts Opacity, twaalf directe Blend-keuzes, Source-transform en ADSR. Onderaan Overdub, Latch, Cue, Cue vast en Panic. | Alle zes genoemde livehandelingen tegelijk bereikbaar. Minder Videos per pagina; kleiner transformvlak dan de derde kandidaat. |
| **Videodeck** | Twaalf Videos krijgen meer ruimte. Rechts Opacity en Blend; het detailpaneel wisselt tussen Transform, ADSR en FX. | Goed wanneer zoeken/selecteren dominant is; Transform en ADSR zijn niet tegelijk zichtbaar. |
| **Gebarenveld** | Groot Source-transformvlak, compacte rij Videos met paginering, daarnaast Opacity, Video loop start, Blend en ADSR. | Meer bewegingsruimte; Blend zit achter een keuzelijst en de thumbnails zijn kleiner. |

De maatvoering in het prototype is aanpasbaar: 48–64 px voor gewone knoppen, 88–128 px hoogte voor de performancepads. Dit is een vergelijking van ruimtegebruik, geen bewezen optimale maat. Voor gebruik op de echte 13-inch iPad is de ruimere kant het startpunt voor de repetitietest.

De voorkeursrichting gebruikt de huidige handelingen en het bestaande Loop-model. Functionele uitbreiding zoals Undo, een A/B-mixer, een live videostream, een volledige layout-editor of presets maakt geen deel uit van deze studie. Een latere implementatiespec hoort als GitHub issue vastgelegd te worden, nadat de gebruiker de richting heeft beoordeeld.
