# Builds resources/discover/awards.json for the Discover tab: winners of well-known awards with their TMDB ids
# and the year of the award, from Wikidata (CC0). Run it again before a release to add the newest winners.
import json, subprocess, time, urllib.parse, os

OUT = os.path.join(os.path.dirname(__file__), '..', 'resources', 'discover', 'awards.json')

# id used by the app, English label of the award on Wikidata, 1 = movies / 2 = series
AWARDS = [
    ('oscar_best_picture', 'Academy Award for Best Picture', 1),
    ('oscar_animated', 'Academy Award for Best Animated Feature', 1),
    ('oscar_international', 'Academy Award for Best International Feature Film', 1),
    ('golden_globe_drama', 'Golden Globe Award for Best Motion Picture – Drama', 1),
    ('palme_dor', "Palme d'Or", 1),
    ('golden_lion', 'Golden Lion', 1),
    ('golden_bear', 'Golden Bear', 1),
    ('emmy_drama', 'Primetime Emmy Award for Outstanding Drama Series', 2),
    ('emmy_comedy', 'Primetime Emmy Award for Outstanding Comedy Series', 2),
]


def sparql(query):
    url = 'https://query.wikidata.org/sparql?format=json&query=' + urllib.parse.quote(query)
    out = subprocess.run(['curl', '-s', '--max-time', '90', '-A',
                          'iptv-player-build/1.0 (https://github.com/murat303/iptv-player)', url],
                         capture_output=True, check=True).stdout
    return json.loads(out)['results']['bindings']


result = []
for key, label, kind in AWARDS:
    prop = 'P4947' if kind == 1 else 'P4983'
    query = '''SELECT ?tmdb (MAX(?y) AS ?year) WHERE {
      ?award rdfs:label "%s"@en .
      ?item p:P166 ?st . ?st ps:P166 ?award .
      OPTIONAL { ?st pq:P585 ?date . BIND(YEAR(?date) AS ?y) }
      ?item wdt:%s ?tmdb .
    } GROUP BY ?tmdb''' % (label.replace('"', '\\"'), prop)
    rows = sparql(query)
    items = []
    for row in rows:
        try:
            tmdb = int(row['tmdb']['value'])
        except ValueError:
            continue
        year = int(row['year']['value']) if 'year' in row else 0
        items.append([tmdb, year])
    # Newest award first
    items.sort(key=lambda i: -i[1])
    print('%-22s %3d titles' % (key, len(items)))
    result.append({'id': key, 'type': kind, 'items': items})
    time.sleep(2)

os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
    json.dump({'source': 'Wikidata (CC0)', 'awards': result}, f, separators=(',', ':'))
    f.write('\n')
print('written', os.path.getsize(OUT), 'bytes')
